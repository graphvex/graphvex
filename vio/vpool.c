#include "vex.h"
#include "vio/vio_internal.h"

;;DEFINITION
/** VideoPool owns growable raw GPU memory slabs obtained from a thin backend
 * driver (Metal, Vulkan, D3D12, wgpu, or the portable software UMA fallback).
 * Slabs grow indefinitely on demand just like re's MemoryPool, and metadata is
 * stored in Graphvex_arena(). */

;;OVERVIEW
/** CLASS: VideoPool (vio/vpool.c)
 * STRUCT FIELDS: driver vtable, SpinLock, default slab size, slab registry,
 * exhaustion counter.
 * PRIVATE HELPERS: software driver callbacks (allocSlab, freeSlab, copy*).
 * FUNCTION REGISTRY: create, createSoftware, destroy, acquireSlab,
 * releaseArenaSlabs, resetArenaSlabs, findByGpu, findByCpu, getters.
 * CONTRACT: GPU addresses (vpointer) are nonzero 64-bit addresses; UMA/shared
 * slabs carry a non-zero cpuBase for O(1) vpointer <-> pointer mapping. */

static bool softwareAllocSlab(any context, VideoMemoryDomain domain, bytes length, VideoSlabInfo *outSlab) {
    (discard) context;
    if (length == 0u || outSlab == nullptr)
        return false;
    any raw = MemoryArena_alloc(Graphvex_arena(), 0, length);
    if (raw == nullptr)
        return false;
    memset(raw, 0, length);
    pointer base = (pointer) raw;
    *outSlab = (VideoSlabInfo) {
        .gpuBase = (vpointer) base,
        .cpuBase = domain == VIDEO_MEMORY_PRIVATE ? (pointer) 0 : base,
        .length = length,
        .driverHandle = raw,
        .domain = domain,
        .umaZeroCopy = domain != VIDEO_MEMORY_PRIVATE,
    };
    return true;
}

static fn softwareFreeSlab(any context, const VideoSlabInfo *slab) {
    (discard) context;
    if (slab != nullptr && VideoSlabInfo_driverHandle(slab) != nullptr)
        MemoryArena_free(Graphvex_arena(), VideoSlabInfo_driverHandle(slab));
}

static bool softwareCopyHostToDevice(any context, const VideoSlabInfo *slab, bytes offset, const_any src, bytes length) {
    (discard) context;
    if (slab == nullptr || VideoSlabInfo_driverHandle(slab) == nullptr || src == nullptr)
        return false;
    bytes slabLen = VideoSlabInfo_length(slab);
    if (offset > slabLen || length > slabLen - offset)
        return false;
    memcpy((u8*) VideoSlabInfo_driverHandle(slab) + offset, src, length);
    return true;
}

static bool softwareCopyDeviceToHost(any context, any dst, const VideoSlabInfo *slab, bytes offset, bytes length) {
    (discard) context;
    if (slab == nullptr || VideoSlabInfo_driverHandle(slab) == nullptr || dst == nullptr)
        return false;
    bytes slabLen = VideoSlabInfo_length(slab);
    if (offset > slabLen || length > slabLen - offset)
        return false;
    memcpy(dst, (const u8*) VideoSlabInfo_driverHandle(slab) + offset, length);
    return true;
}

static fn recordPoolExhaustion(VideoPool *pool, bytes requested) {
    if (VideoPool_exhaustionCountField(pool) != UINT64_MAX)
        VideoPool_setExhaustionCountField(pool, VideoPool_exhaustionCountField(pool) + 1u);
    if (!VideoPool_exhaustionReported(pool)) {
        THROW("VideoPool: slab allocation failed for %zu bytes", requested);
        VideoPool_setExhaustionReported(pool, true);
    }
}

;;CONSTRUCTOR
VideoPool *VideoPool_2(const VideoPoolDriver *driver, bytes slabBytes)
{
    if (driver == nullptr || !VideoPoolDriver_hasAlloc(driver)) {
        THROW("VideoPool: rejected invalid driver");
        return nullptr;
    }
    VideoPool *pool = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *pool);
    if (pool == nullptr) {
        THROW("VideoPool: failed to allocate pool metadata");
        return nullptr;
    }
    memset(pool, 0, sizeof *pool);
    VideoPool_setDriver(pool, *driver);
    VideoPool_setSlabBytesField(pool, slabBytes == 0u ? VIDEO_MEMORY_DEFAULT_SLAB_BYTES : slabBytes);
    return pool;
}

VideoPool *VideoPool_createDefault(bytes slabBytes) {
#if defined(VEX_PLATFORM_APPLE)
    VideoPool *metal = VideoPool_createMetal(slabBytes);
    if (metal != nullptr)
        return metal;
#endif
    return VideoPool_createSoftware(slabBytes);
}

VideoPool *VideoPool_createSoftware(bytes slabBytes) {
    VideoPoolDriver driver = {
        .name = "software-uma",
        .context = nullptr,
        .uma = true,
        .allocSlab = softwareAllocSlab,
        .freeSlab = softwareFreeSlab,
        .copyHostToDevice = softwareCopyHostToDevice,
        .copyDeviceToHost = softwareCopyDeviceToHost,
        .destroyContext = nullptr,
    };
    return VideoPool(&driver, slabBytes);
}

fn VideoPool_destroy(VideoPool *pool) {
    if (pool == nullptr)
        return;
    SpinLock_lock(VideoPool_lockPtr(pool));
    VideoPoolDriver driver = VideoPool_driver(pool);
    VideoSlabRecord *slabs = VideoPool_slabs(pool);
    bytes count = VideoPool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i)
        driver.freeSlab(driver.context, &slabs[i].info);
    MemoryArena_free(Graphvex_arena(), slabs);
    VideoPool_setSlabs(pool, nullptr);
    VideoPool_setSlabCountField(pool, 0u);
    VideoPool_setSlabCapacity(pool, 0u);
    SpinLock_unlock(VideoPool_lockPtr(pool));
    if (driver.destroyContext != nullptr)
        driver.destroyContext(driver.context);
    MemoryArena_free(Graphvex_arena(), pool);
}

VideoSlabRecord *VideoPool_acquireSlab(VideoPool *pool, VideoArena *owner, VideoMemoryDomain domain, bytes minBytes) {
    if (pool == nullptr || owner == nullptr)
        return nullptr;
    bytes want = VideoPool_slabBytesField(pool);
    if (minBytes > want)
        want = minBytes;
    SpinLock_lock(VideoPool_lockPtr(pool));
    VideoSlabRecord *slabs = VideoPool_slabs(pool);
    bytes count = VideoPool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i) {
        if (!slabs[i].inUse && slabs[i].info.domain == domain && slabs[i].info.length >= minBytes) {
            slabs[i].owner = owner;
            slabs[i].cursor = 0u;
            slabs[i].liveBytes = 0u;
            slabs[i].inUse = true;
            SpinLock_unlock(VideoPool_lockPtr(pool));
            return &slabs[i];
        }
    }
    if (count == VideoPool_slabCapacity(pool)) {
        bytes nextCap = VideoPool_slabCapacity(pool) == 0u ? 8u : VideoPool_slabCapacity(pool) * 2u;
        VideoSlabRecord *grown = MemoryArena_realloc(Graphvex_arena(), slabs, nextCap * sizeof *grown);
        if (grown == nullptr) {
            recordPoolExhaustion(pool, nextCap * sizeof *grown);
            SpinLock_unlock(VideoPool_lockPtr(pool));
            return nullptr;
        }
        VideoPool_setSlabs(pool, grown);
        VideoPool_setSlabCapacity(pool, nextCap);
        slabs = grown;
    }
    VideoPoolDriver driver = VideoPool_driver(pool);
    VideoSlabInfo info = {0};
    if (!driver.allocSlab(driver.context, domain, want, &info) || info.gpuBase == 0u || info.length < minBytes) {
        recordPoolExhaustion(pool, want);
        SpinLock_unlock(VideoPool_lockPtr(pool));
        return nullptr;
    }
    slabs[count] = (VideoSlabRecord) {
        .info = info,
        .owner = owner,
        .cursor = 0u,
        .liveBytes = 0u,
        .inUse = true,
    };
    VideoPool_setSlabCountField(pool, count + 1u);
    SpinLock_unlock(VideoPool_lockPtr(pool));
    return &slabs[count];
}

fn VideoPool_releaseArenaSlabs(VideoPool *pool, VideoArena *owner) {
    if (pool == nullptr || owner == nullptr)
        return;
    SpinLock_lock(VideoPool_lockPtr(pool));
    VideoSlabRecord *slabs = VideoPool_slabs(pool);
    bytes count = VideoPool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i) {
        if (slabs[i].owner == owner) {
            slabs[i].owner = nullptr;
            slabs[i].cursor = 0u;
            slabs[i].liveBytes = 0u;
            slabs[i].inUse = false;
        }
    }
    SpinLock_unlock(VideoPool_lockPtr(pool));
}

fn VideoPool_resetArenaSlabs(VideoPool *pool, VideoArena *owner) {
    if (pool == nullptr || owner == nullptr)
        return;
    SpinLock_lock(VideoPool_lockPtr(pool));
    VideoSlabRecord *slabs = VideoPool_slabs(pool);
    bytes count = VideoPool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i) {
        if (slabs[i].owner == owner) {
            slabs[i].cursor = 0u;
            slabs[i].liveBytes = 0u;
        }
    }
    SpinLock_unlock(VideoPool_lockPtr(pool));
}

bool VideoPool_findByGpu(const VideoPool *pool, vpointer addr, bytes length, VideoSlabInfo *outInfo, bytes *outOffset) {
    if (pool == nullptr || addr == 0u)
        return false;
    SpinLock_lock(VideoPool_lockPtr((VideoPool*) pool));
    VideoSlabRecord *slabs = VideoPool_slabs(pool);
    bytes count = VideoPool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i) {
        VideoSlabInfo info = slabs[i].info;
        if (addr >= info.gpuBase && addr < info.gpuBase + info.length) {
            bytes offset = (bytes) (addr - info.gpuBase);
            if (length <= info.length - offset) {
                if (outInfo != nullptr)
                    *outInfo = info;
                if (outOffset != nullptr)
                    *outOffset = offset;
                SpinLock_unlock(VideoPool_lockPtr((VideoPool*) pool));
                return true;
            }
            break;
        }
    }
    SpinLock_unlock(VideoPool_lockPtr((VideoPool*) pool));
    return false;
}

bool VideoPool_findByCpu(const VideoPool *pool, pointer cpuAddr, VideoSlabInfo *outInfo, bytes *outOffset) {
    if (pool == nullptr || cpuAddr == 0u)
        return false;
    SpinLock_lock(VideoPool_lockPtr((VideoPool*) pool));
    VideoSlabRecord *slabs = VideoPool_slabs(pool);
    bytes count = VideoPool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i) {
        VideoSlabInfo info = slabs[i].info;
        if (info.cpuBase != 0u && cpuAddr >= info.cpuBase && cpuAddr < info.cpuBase + info.length) {
            if (outInfo != nullptr)
                *outInfo = info;
            if (outOffset != nullptr)
                *outOffset = (bytes) (cpuAddr - info.cpuBase);
            SpinLock_unlock(VideoPool_lockPtr((VideoPool*) pool));
            return true;
        }
    }
    SpinLock_unlock(VideoPool_lockPtr((VideoPool*) pool));
    return false;
}

bool VideoPool_isUma(const VideoPool *pool) {
    return pool != nullptr && VideoPool_driver(pool).uma;
}

const utf8 *VideoPool_driverName(const VideoPool *pool) {
    return pool == nullptr ? nullptr : VideoPool_driver(pool).name;
}

bytes VideoPool_slabBytes(const VideoPool *pool) {
    return pool == nullptr ? 0u : VideoPool_slabBytesField(pool);
}

bytes VideoPool_slabCount(const VideoPool *pool) {
    return pool == nullptr ? 0u : VideoPool_slabCountField(pool);
}

bytes VideoPool_totalBytes(const VideoPool *pool) {
    if (pool == nullptr)
        return 0u;
    SpinLock_lock(VideoPool_lockPtr((VideoPool*) pool));
    bytes total = 0u;
    VideoSlabRecord *slabs = VideoPool_slabs(pool);
    bytes count = VideoPool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i)
        total += slabs[i].info.length;
    SpinLock_unlock(VideoPool_lockPtr((VideoPool*) pool));
    return total;
}

bytes VideoPool_usedBytes(const VideoPool *pool) {
    if (pool == nullptr)
        return 0u;
    SpinLock_lock(VideoPool_lockPtr((VideoPool*) pool));
    bytes used = 0u;
    VideoSlabRecord *slabs = VideoPool_slabs(pool);
    bytes count = VideoPool_slabCountField(pool);
    for (bytes i = 0u; i < count; ++i)
        if (slabs[i].inUse)
            used += slabs[i].liveBytes;
    SpinLock_unlock(VideoPool_lockPtr((VideoPool*) pool));
    return used;
}

u64 VideoPool_exhaustionCount(const VideoPool *pool) {
    return pool == nullptr ? 0u : VideoPool_exhaustionCountField(pool);
}
