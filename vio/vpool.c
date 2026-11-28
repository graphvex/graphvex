#include "vex.h"
#include "vio/vio_internal.h"
#include "core/graphics.h"

#include <string.h>

#if defined(VEX_PLATFORM_APPLE) || defined(VEX_PLATFORM_APPLE_SILICON) || defined(VEX_PLATFORM_APPLE_X86_64) || defined(VEX_PLATFORM_LINUX_X86_64) || defined(VEX_PLATFORM_LINUX_ARM64)
#include <sys/mman.h>
#elif defined(VEX_PLATFORM_WINDOWS_X86_64) || defined(VEX_PLATFORM_WINDOWS_ARM64)
#include <windows.h>
#endif

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ImagePool
 * ============================================================================
 * Owns growable raw GPU image memory slabs obtained from a thin backend
 * driver (Metal, Vulkan, D3D12, wgpu, or the portable software UMA fallback).
 * Slabs grow on demand with PROT_NONE guard pages; metadata lives in
 * Graphvex_arena().
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ImagePool (vio/vpool.c)
 * ============================================================================
 * Growable GPU slab pool backing ImageArena and GraphicsImage allocations.
 *
 * --FIELDS-------------------------------
 * private  ImagePoolDriver   driver              backend slab vtable
 * private  SpinLock          lock                synchronizes slab table
 * private  bytes             slabBytes           default slab size
 * private  ImageSlabRecord  *slabs               owns slab array, 1:N
 * private  bytes             slabCount           active slab count
 * private  bytes             slabCapacity        slab array capacity
 * private  u64               exhaustionCount     refused slab allocations
 * private  bool              exhaustionReported  one-shot THROW flag
 *
 * --CONSTRUCTORS-------------------------
 * public   ImagePool_1(...)                      default Metal or software pool
 * public   ImagePool_2(...)                      explicit driver pool
 * public   ImagePool_software(...)               software UMA fallback pool
 *
 * --FUNCTIONS----------------------------
 * public   ImagePool_destroy(...)                frees all slabs and metadata
 * public   ImagePool_acquireSlab(...)            assigns or allocates a slab
 * public   ImagePool_prepareSlabForUse(...)      marks slab active before use
 * public   ImagePool_releaseArenaSlabs(...)      recycles arena slabs
 * public   ImagePool_resetArenaSlabs(...)        resets or trims arena slabs
 * public   ImagePool_findByGpu(...)              resolves GPU vpointer to slab
 * public   ImagePool_findByCpu(...)              resolves CPU pointer to slab
 *
 * --GETTERS------------------------------
 * public   ImagePool_isUma/driverName/slabBytes/slabCount/totalBytes/usedBytes/exhaustionCount
 * ============================================================================
 */

static bytes swRoundToPage(bytes n)
{
    bytes page = (bytes) MEMORY_PAGE_SIZE;
    bytes mask = page - 1u;
    if (n > SIZE_MAX - mask)
        return 0u;

    return (n + mask) & ~mask;
}

;;SETTER
static fn imageSlabInfoSet(ImageSlabInfo *s, vpointer gpuBase, pointer cpuBase,
                           bytes length, any driverHandle,
                           ImageMemoryDomain domain, bool umaZeroCopy)
{
    if (!s)
        return;

    (*s).gpuBase = gpuBase;
    (*s).cpuBase = cpuBase;
    (*s).length = length;
    (*s).driverHandle = driverHandle;
    (*s).domain = domain;
    (*s).umaZeroCopy = umaZeroCopy;
}

;;SETTER
static fn imageSlabRecordInit(ImageSlabRecord *r, ImageSlabInfo info, ImageArena *owner)
{
    if (!r)
        return;

    (*r).info = info;
    (*r).owner = owner;
    (*r).cursor = 0u;
    (*r).liveBytes = 0u;
    (*r).idleResets = 0u;
    (*r).inUse = true;
    (*r).reusable = false;
}

static bool softwareAllocSlab(any context, ImageMemoryDomain domain, bytes length, ImageSlabInfo *outSlab)
{
    (discard) context;
    if (length == 0u || outSlab == nullptr)
        return false;

    bytes page = (bytes) MEMORY_PAGE_SIZE;
    bytes alignedLen = swRoundToPage(length);
    if (alignedLen == 0u || alignedLen > SIZE_MAX - (2u * page))
        return false;

    bytes totalLen = alignedLen + (2u * page);
    u8 *payload = nullptr;
#if defined(VEX_PLATFORM_APPLE) || defined(VEX_PLATFORM_APPLE_SILICON) || defined(VEX_PLATFORM_APPLE_X86_64) || defined(VEX_PLATFORM_LINUX_X86_64) || defined(VEX_PLATFORM_LINUX_ARM64)
    any mapping = mmap(nullptr, totalLen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapping == MAP_FAILED)
        return false;

    u8 *mapBase = (u8*) mapping;
    if (mprotect(mapBase, page, PROT_NONE) != 0 ||
        mprotect(mapBase + page + alignedLen, page, PROT_NONE) != 0) {
        (discard) munmap(mapBase, totalLen);
        return false;
    }
    payload = mapBase + page;
#elif defined(VEX_PLATFORM_WINDOWS_X86_64) || defined(VEX_PLATFORM_WINDOWS_ARM64)
    any mapping = VirtualAlloc(nullptr, (SIZE_T) totalLen, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (mapping == nullptr)
        return false;

    u8 *mapBase = (u8*) mapping;
    DWORD oldProt = 0;
    if (!VirtualProtect(mapBase, (SIZE_T) page, PAGE_NOACCESS, &oldProt) ||
        !VirtualProtect(mapBase + page + alignedLen, (SIZE_T) page, PAGE_NOACCESS, &oldProt)) {
        (discard) VirtualFree(mapBase, 0, MEM_RELEASE);
        return false;
    }
    payload = mapBase + page;
#else
    return false;
#endif
    pointer base = (pointer) payload;
    pointer cpuBase = domain == IMAGE_MEMORY_PRIVATE ? (pointer) 0 : base;
    bool umaZero = domain != IMAGE_MEMORY_PRIVATE;
    imageSlabInfoSet(outSlab, (vpointer) base, cpuBase, alignedLen, payload, domain, umaZero);
    return true;
}

static fn softwareFreeSlab(any context, const ImageSlabInfo *slab)
{
    (discard) context;
    if (slab == nullptr || ImageSlabInfo_driverHandle(slab) == nullptr)
        return;

    bytes page = (bytes) MEMORY_PAGE_SIZE;
    bytes alignedLen = ImageSlabInfo_length(slab);
    u8 *payload = (u8*) ImageSlabInfo_driverHandle(slab);
    memset(payload, 0, alignedLen);
    u8 *mapBase = payload - page;
    bytes totalLen = alignedLen + (2u * page);
#if defined(VEX_PLATFORM_APPLE) || defined(VEX_PLATFORM_APPLE_SILICON) || defined(VEX_PLATFORM_APPLE_X86_64) || defined(VEX_PLATFORM_LINUX_X86_64) || defined(VEX_PLATFORM_LINUX_ARM64)
    (discard) munmap(mapBase, totalLen);
#elif defined(VEX_PLATFORM_WINDOWS_X86_64) || defined(VEX_PLATFORM_WINDOWS_ARM64)
    (discard) totalLen;
    (discard) VirtualFree(mapBase, 0, MEM_RELEASE);
#else
    (discard) mapBase;
    (discard) totalLen;
#endif
}

static bool softwareCopyHostToDevice(any context, const ImageSlabInfo *slab, bytes offset, const_any src, bytes length)
{
    (discard) context;
    if (slab == nullptr || ImageSlabInfo_driverHandle(slab) == nullptr || src == nullptr)
        return false;

    bytes slabLen = ImageSlabInfo_length(slab);
    if (offset > slabLen || length > slabLen - offset)
        return false;

    memcpy((u8*) ImageSlabInfo_driverHandle(slab) + offset, src, length);
    return true;
}

static bool softwareCopyDeviceToHost(any context, any dst, const ImageSlabInfo *slab, bytes offset, bytes length)
{
    (discard) context;
    if (slab == nullptr || ImageSlabInfo_driverHandle(slab) == nullptr || dst == nullptr)
        return false;

    bytes slabLen = ImageSlabInfo_length(slab);
    if (offset > slabLen || length > slabLen - offset)
        return false;

    memcpy(dst, (const u8*) ImageSlabInfo_driverHandle(slab) + offset, length);
    return true;
}

static fn recordPoolExhaustion(ImagePool *pool, bytes requested)
{
    if (ImagePool_exhaustionCountField(pool) != UINT64_MAX)
        ImagePool_setExhaustionCountField(pool, ImagePool_exhaustionCountField(pool) + 1u);

    if (!ImagePool_exhaustionReported(pool)) {
        THROW("ImagePool: slab allocation failed for %zu bytes", requested);
        ImagePool_setExhaustionReported(pool, true);
    }
}

;;CONSTRUCTOR
/**
 * Creates an ImagePool backed by an explicit ImagePoolDriver vtable.
 * @param driver    backend slab driver vtable
 * @param slabBytes default slab size in bytes (0 selects default)
 * @return newly allocated ImagePool, or nullptr on failure
 */
ImagePool *ImagePool_2(const ImagePoolDriver *driver, bytes slabBytes)
{
    if (driver == nullptr || !ImagePoolDriver_hasAlloc(driver)) {
        THROW("ImagePool: rejected invalid driver");
        return nullptr;
    }
    ImagePool *pool = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *pool);
    if (pool == nullptr) {
        THROW("ImagePool: failed to allocate pool metadata");
        return nullptr;
    }
    memset(pool, 0, sizeof *pool);
    ImagePool_setDriver(pool, *driver);
    ImagePool_setSlabBytesField(pool, slabBytes == 0u ? IMAGE_MEMORY_DEFAULT_SLAB_BYTES : slabBytes);
    return pool;
}

;;CONSTRUCTOR
/**
 * Creates the default ImagePool (Metal on Apple when available, else software UMA).
 * @param slabBytes slab size in bytes (0 selects IMAGE_MEMORY_DEFAULT_SLAB_BYTES)
 * @return newly allocated ImagePool, or nullptr on failure
 */
ImagePool *ImagePool_1(bytes slabBytes)
{
    Graphics_latchBackend();
    GraphicsBackend requested = GRAPHICS_BACKEND();
    if (requested == GRAPHICS_SOFTWARE)
        return ImagePool_software(slabBytes);

    if (requested == GRAPHICS_VULKAN || requested == GRAPHICS_D3D12 || requested == GRAPHICS_WGPU) {
        THROW("ImagePool: requested backend %u is not linked on this host; falling back to software-uma", (u32) requested);
        return ImagePool_software(slabBytes);
    }
#if defined(VEX_PLATFORM_APPLE)
    ImagePool *metal = ImagePool_metal(slabBytes);
    if (metal != nullptr)
        return metal;
#endif
    return ImagePool_software(slabBytes);
}

;;CONSTRUCTOR
/**
 * Creates a software UMA ImagePool backed by guard-paged host memory.
 * @param slabBytes slab size in bytes (0 selects IMAGE_MEMORY_DEFAULT_SLAB_BYTES)
 * @return newly allocated ImagePool, or nullptr on failure
 */
ImagePool *ImagePool_software(bytes slabBytes)
{
    ImagePoolDriver driver;
    memset(&driver, 0, sizeof(ImagePoolDriver));
    driver.name = "software-uma";
    driver.context = nullptr;
    driver.uma = true;
    driver.allocSlab = softwareAllocSlab;
    driver.freeSlab = softwareFreeSlab;
    driver.copyHostToDevice = softwareCopyHostToDevice;
    driver.copyDeviceToHost = softwareCopyDeviceToHost;
    driver.destroyContext = nullptr;
    return ImagePool(&driver, slabBytes);
}

;;DECONSTRUCT
fn ImagePool_destroy(ImagePool *pool)
{
    if (pool == nullptr)
        return;

    SpinLock_lock(ImagePool_lockPtr(pool));
    ImagePoolDriver driver = ImagePool_driver(pool);
    ImageSlabRecord *slabs = ImagePool_slabs(pool);
    bytes count = ImagePool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i)
        driver.freeSlab(driver.context, &slabs[i].info);

    MemoryArena_free(Graphvex_arena(), slabs);
    ImagePool_setSlabs(pool, nullptr);
    ImagePool_setSlabCountField(pool, 0u);
    ImagePool_setSlabCapacity(pool, 0u);
    SpinLock_unlock(ImagePool_lockPtr(pool));
    if (driver.destroyContext != nullptr)
        driver.destroyContext(driver.context);

    MemoryArena_free(Graphvex_arena(), pool);
}

static fn ipoolAdviseReusable(pointer cpuBase, bytes length)
{
    if (cpuBase == 0u || length == 0u)
        return;

#if defined(VEX_PLATFORM_APPLE) || defined(VEX_PLATFORM_APPLE_SILICON) || defined(VEX_PLATFORM_APPLE_X86_64) || defined(VEX_PLATFORM_LINUX_X86_64) || defined(VEX_PLATFORM_LINUX_ARM64)
#if defined(MADV_FREE_REUSABLE)
    (discard) madvise((any) cpuBase, length, MADV_FREE_REUSABLE);
#elif defined(MADV_FREE)
    (discard) madvise((any) cpuBase, length, MADV_FREE);
#elif defined(MADV_DONTNEED)
    (discard) madvise((any) cpuBase, length, MADV_DONTNEED);
#endif
#endif
}

static fn ipoolAdviseReuse(pointer cpuBase, bytes length)
{
    if (cpuBase == 0u || length == 0u)
        return;

#if defined(MADV_FREE_REUSE)
    (discard) madvise((any) cpuBase, length, MADV_FREE_REUSE);
#endif
}

fn ImagePool_prepareSlabForUse(ImageSlabRecord *slab)
{
    if (!slab)
        return;

    if (ImageSlabRecord_reusable(slab)) {
        ImageSlabInfo info = ImageSlabRecord_info(slab);
        ipoolAdviseReuse(info.cpuBase, info.length);
        ImageSlabRecord_setReusable(slab, false);
    }
    ImageSlabRecord_setIdleResets(slab, 0u);
}

ImageSlabRecord *ImagePool_acquireSlab(ImagePool *pool, ImageArena *owner, ImageMemoryDomain domain, bytes minBytes)
{
    if (pool == nullptr || owner == nullptr)
        return nullptr;

    bytes want = ImagePool_slabBytesField(pool);
    if (minBytes > want)
        want = minBytes;

    SpinLock_lock(ImagePool_lockPtr(pool));
    ImageSlabRecord *slabs = ImagePool_slabs(pool);
    bytes count = ImagePool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i) {
        if (!slabs[i].inUse && slabs[i].info.domain == domain && slabs[i].info.length >= minBytes) {
            ImagePool_prepareSlabForUse(&slabs[i]);
            slabs[i].owner = owner;
            slabs[i].cursor = 0u;
            slabs[i].liveBytes = 0u;
            slabs[i].inUse = true;
            SpinLock_unlock(ImagePool_lockPtr(pool));
            return &slabs[i];
        }
    }
    if (count == ImagePool_slabCapacity(pool)) {
        bytes nextCap = ImagePool_slabCapacity(pool) == 0u ? 8u : ImagePool_slabCapacity(pool) * 2u;
        ImageSlabRecord *grown = MemoryArena_realloc(Graphvex_arena(), slabs, nextCap * sizeof *grown);
        if (grown == nullptr) {
            recordPoolExhaustion(pool, nextCap * sizeof *grown);
            SpinLock_unlock(ImagePool_lockPtr(pool));
            return nullptr;
        }
        ImagePool_setSlabs(pool, grown);
        ImagePool_setSlabCapacity(pool, nextCap);
        slabs = grown;
    }
    ImagePoolDriver driver = ImagePool_driver(pool);
    ImageSlabInfo info;
    memset(&info, 0, sizeof(ImageSlabInfo));
    if (!driver.allocSlab(driver.context, domain, want, &info) || info.gpuBase == 0u || info.length < minBytes) {
        recordPoolExhaustion(pool, want);
        SpinLock_unlock(ImagePool_lockPtr(pool));
        return nullptr;
    }
    imageSlabRecordInit(&slabs[count], info, owner);
    ImagePool_setSlabCountField(pool, count + 1u);
    SpinLock_unlock(ImagePool_lockPtr(pool));
    return &slabs[count];
}

fn ImagePool_releaseArenaSlabs(ImagePool *pool, ImageArena *owner)
{
    if (pool == nullptr || owner == nullptr)
        return;

    SpinLock_lock(ImagePool_lockPtr(pool));
    ImagePoolDriver driver = ImagePool_driver(pool);
    bytes defaultSlab = ImagePool_slabBytesField(pool);
    ImageSlabRecord *slabs = ImagePool_slabs(pool);
    bytes count = ImagePool_slabCountField(pool);
    bytes writeIdx = 0u;
    for (bytes i = 0u; i < count; ++i) {
        if (slabs[i].owner == owner) {
            if (slabs[i].info.length > defaultSlab) {
                driver.freeSlab(driver.context, &slabs[i].info);
                continue;
            }
            slabs[i].owner = nullptr;
            slabs[i].cursor = 0u;
            slabs[i].liveBytes = 0u;
            slabs[i].inUse = false;
            ImageSlabRecord_setIdleResets(&slabs[i], 0u);
            ImageSlabRecord_setReusable(&slabs[i], true);
            ipoolAdviseReusable(slabs[i].info.cpuBase, slabs[i].info.length);
        }
        if (writeIdx != i)
            slabs[writeIdx] = slabs[i];

        writeIdx++;
    }
    ImagePool_setSlabCountField(pool, writeIdx);
    SpinLock_unlock(ImagePool_lockPtr(pool));
}

bytes ImagePool_resetArenaSlabs(ImagePool *pool, ImageArena *owner, bool aggressive)
{
    if (pool == nullptr || owner == nullptr)
        return 0u;

    SpinLock_lock(ImagePool_lockPtr(pool));
    ImagePoolDriver driver = ImagePool_driver(pool);
    bytes defaultSlab = ImagePool_slabBytesField(pool);
    ImageSlabRecord *slabs = ImagePool_slabs(pool);
    bytes count = ImagePool_slabCountField(pool);
    bytes writeIdx = 0u;
    bytes remainingCap = 0u;
    bool keptBase = false;
    bool keptOversized = false;
    for (bytes i = 0u; i < count; ++i) {
        if (slabs[i].owner != owner) {
            if (writeIdx != i)
                slabs[writeIdx] = slabs[i];

            writeIdx++;
            continue;
        }
        bool wasUsed = slabs[i].cursor > 0u;
        bool isOversized = slabs[i].info.length > defaultSlab;
        bool keep = false;
        if (aggressive) {
            keep = !isOversized && !keptBase;
        } else if (!isOversized) {
            if (!keptBase || wasUsed || ImageSlabRecord_idleResets(&slabs[i]) == 0u)
                keep = true;
        } else {
            keep = wasUsed && !keptOversized;
        }
        if (!keep) {
            driver.freeSlab(driver.context, &slabs[i].info);
            continue;
        }
        if (isOversized)
            keptOversized = true;
        else
            keptBase = true;

        slabs[i].cursor = 0u;
        slabs[i].liveBytes = 0u;
        u32 nextIdle = wasUsed ? 0u : ImageSlabRecord_idleResets(&slabs[i]) + 1u;
        ImageSlabRecord_setIdleResets(&slabs[i], nextIdle);
        ImageSlabRecord_setReusable(&slabs[i], true);
        ipoolAdviseReusable(slabs[i].info.cpuBase, slabs[i].info.length);
        remainingCap += slabs[i].info.length;
        if (writeIdx != i)
            slabs[writeIdx] = slabs[i];

        writeIdx++;
    }
    ImagePool_setSlabCountField(pool, writeIdx);
    SpinLock_unlock(ImagePool_lockPtr(pool));
    return remainingCap;
}

bool ImagePool_findByGpu(const ImagePool *pool, vpointer addr, bytes length, ImageSlabInfo *outInfo, bytes *outOffset)
{
    if (pool == nullptr || addr == 0u)
        return false;

    SpinLock_lock(ImagePool_lockPtr((ImagePool*) pool));
    ImageSlabRecord *slabs = ImagePool_slabs(pool);
    bytes count = ImagePool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i) {
        ImageSlabInfo info = slabs[i].info;
        if (addr >= info.gpuBase && addr < info.gpuBase + info.length) {
            bytes offset = (bytes) (addr - info.gpuBase);
            if (length <= info.length - offset) {
                if (outInfo != nullptr)
                    *outInfo = info;

                if (outOffset != nullptr)
                    *outOffset = offset;

                SpinLock_unlock(ImagePool_lockPtr((ImagePool*) pool));
                return true;
            }
            break;
        }
    }
    SpinLock_unlock(ImagePool_lockPtr((ImagePool*) pool));
    return false;
}

bool ImagePool_findByCpu(const ImagePool *pool, pointer cpuAddr, ImageSlabInfo *outInfo, bytes *outOffset)
{
    if (pool == nullptr || cpuAddr == 0u)
        return false;

    SpinLock_lock(ImagePool_lockPtr((ImagePool*) pool));
    ImageSlabRecord *slabs = ImagePool_slabs(pool);
    bytes count = ImagePool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i) {
        ImageSlabInfo info = slabs[i].info;
        if (info.cpuBase != 0u && cpuAddr >= info.cpuBase && cpuAddr < info.cpuBase + info.length) {
            if (outInfo != nullptr)
                *outInfo = info;

            if (outOffset != nullptr)
                *outOffset = (bytes) (cpuAddr - info.cpuBase);

            SpinLock_unlock(ImagePool_lockPtr((ImagePool*) pool));
            return true;
        }
    }
    SpinLock_unlock(ImagePool_lockPtr((ImagePool*) pool));
    return false;
}

bool ImagePool_isUma(const ImagePool *pool)
{
    if (pool == nullptr)
        return false;

    return ImagePool_driver(pool).uma;
}

string ImagePool_driverName(const ImagePool *pool)
{
    if (pool == nullptr)
        return nullptr;

    return ImagePool_driver(pool).name;
}

bytes ImagePool_slabBytes(const ImagePool *pool)
{
    if (pool == nullptr)
        return 0u;

    return ImagePool_slabBytesField(pool);
}

bytes ImagePool_slabCount(const ImagePool *pool)
{
    if (pool == nullptr)
        return 0u;

    return ImagePool_slabCountField(pool);
}

bytes ImagePool_totalBytes(const ImagePool *pool)
{
    if (pool == nullptr)
        return 0u;

    SpinLock_lock(ImagePool_lockPtr((ImagePool*) pool));
    bytes total = 0u;
    ImageSlabRecord *slabs = ImagePool_slabs(pool);
    bytes count = ImagePool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i)
        total += slabs[i].info.length;

    SpinLock_unlock(ImagePool_lockPtr((ImagePool*) pool));
    return total;
}

bytes ImagePool_usedBytes(const ImagePool *pool)
{
    if (pool == nullptr)
        return 0u;

    SpinLock_lock(ImagePool_lockPtr((ImagePool*) pool));
    bytes used = 0u;
    ImageSlabRecord *slabs = ImagePool_slabs(pool);
    bytes count = ImagePool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i) {
        if (slabs[i].inUse)
            used += slabs[i].liveBytes;
    }
    SpinLock_unlock(ImagePool_lockPtr((ImagePool*) pool));
    return used;
}

u64 ImagePool_exhaustionCount(const ImagePool *pool)
{
    if (pool == nullptr)
        return 0u;

    return ImagePool_exhaustionCountField(pool);
}
