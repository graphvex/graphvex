#include "vex.h"
#include "vio/vio_internal.h"

#include <string.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ImageArena
 * ============================================================================
 * Per-subsystem virtual GPU arena carved from an ImagePool. Sub-allocates
 * vpointer ranges with power-of-two alignment (floor 32B, default 256B),
 * recycles freed blocks on a free list, supports O(1) reset for per-frame
 * transient GPU scratch, and enforces optional hard byte budgets.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ImageArena (vio/varena.c)
 * ============================================================================
 * Virtual GPU arena sub-allocator over ImagePool slabs.
 *
 * --CONSTRUCTORS-------------------------
 * public   ImageArena_3(...)          constructs arena over ImagePool
 *
 * --FUNCTIONS----------------------------
 * public   ImageArena_destroy(...)    releases arena bookkeeping and slabs
 * public   ImageArena_alloc(...)      allocates aligned vpointer range
 * public   ImageArena_free(...)       recycles vpointer block
 * public   ImageArena_reset(...)      resets arena cursors in O(1)
 * public   ImageArena_trim(...)       aggressively returns idle slabs
 *
 * --GETTERS------------------------------
 * public   ImageArena_pool/domain/usedBytes/capacity/maxBytes/exhaustionCount
 * ============================================================================
 */

static bool isPow2(bytes n)
{
    return n != 0u && (n & (n - 1u)) == 0u;
}

static bytes alignUp(bytes value, bytes alignment)
{
    bytes mask = alignment - 1u;
    if (value > SIZE_MAX - mask)
        return 0u;

    return (value + mask) & ~mask;
}

static fn recordArenaExhaustion(ImageArena *arena, bytes requested)
{
    if (ImageArena_exhaustionCountField(arena) != UINT64_MAX)
        ImageArena_setExhaustionCountField(arena, ImageArena_exhaustionCountField(arena) + 1u);

    if (!ImageArena_exhaustionReported(arena)) {
        THROW("ImageArena: allocation refused for %zu bytes", requested);
        ImageArena_setExhaustionReported(arena, true);
    }
}

static bool trackLiveBlock(ImageArena *arena, vpointer addr, bytes length)
{
    ImageLiveBlock *node = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *node);
    if (node == nullptr)
        return false;

    ImageLiveBlock_setAddr(node, addr);
    ImageLiveBlock_setLength(node, length);
    ImageLiveBlock_setNext(node, ImageArena_liveHead(arena));
    ImageArena_setLiveHead(arena, node);
    return true;
}

static fn clearBookkeeping(ImageArena *arena)
{
    ImageFreeBlock *f = ImageArena_freeHead(arena);
    while (f != nullptr) {
        ImageFreeBlock *next = ImageFreeBlock_next(f);
        MemoryArena_free(Graphvex_arena(), f);
        f = next;
    }
    ImageArena_setFreeHead(arena, nullptr);
    ImageLiveBlock *l = ImageArena_liveHead(arena);
    while (l != nullptr) {
        ImageLiveBlock *next = ImageLiveBlock_next(l);
        MemoryArena_free(Graphvex_arena(), l);
        l = next;
    }
    ImageArena_setLiveHead(arena, nullptr);
}

;;CONSTRUCTOR
/**
 * Constructs a virtual ImageArena carved from pool.
 * @param pool     parent ImagePool
 * @param domain   memory domain (shared, private, staging)
 * @param maxBytes optional hard byte cap (0 = unbounded)
 * @return owned ImageArena pointer, or nullptr on failure
 */
ImageArena *ImageArena_3(ImagePool *pool, ImageMemoryDomain domain, bytes maxBytes)
{
    if (pool == nullptr) {
        THROW("ImageArena: rejected null ImagePool");
        return nullptr;
    }
    ImageArena *arena = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *arena);
    if (arena == nullptr) {
        THROW("ImageArena: failed to allocate arena metadata");
        return nullptr;
    }
    memset(arena, 0, sizeof *arena);
    ImageArena_setPoolField(arena, pool);
    ImageArena_setDomainField(arena, domain);
    ImageArena_setMaxBytesField(arena, maxBytes);
    return arena;
}

;;DECONSTRUCT
fn ImageArena_destroy(ImageArena *arena)
{
    if (arena == nullptr)
        return;

    SpinLock_lock(ImageArena_lockPtr(arena));
    clearBookkeeping(arena);
    ImagePool *pool = ImageArena_poolField(arena);
    ImageArena_setUsedBytesField(arena, 0u);
    ImageArena_setCapacityBytesField(arena, 0u);
    SpinLock_unlock(ImageArena_lockPtr(arena));
    ImagePool_releaseArenaSlabs(pool, arena);
    MemoryArena_free(Graphvex_arena(), arena);
}

vpointer ImageArena_alloc(ImageArena *arena, bytes numBytes, bytes alignment)
{
    if (arena == nullptr || numBytes == 0u)
        return 0u;

    bytes align = alignment == 0u ? IMAGE_MEMORY_DEFAULT_ALIGN : alignment;
    if (align < IMAGE_MEMORY_ALIGN_FLOOR)
        align = IMAGE_MEMORY_ALIGN_FLOOR;

    if (!isPow2(align)) {
        THROW("ImageArena: alignment must be power of two, got %zu", align);
        return 0u;
    }
    bytes size = alignUp(numBytes, IMAGE_MEMORY_ALIGN_FLOOR);
    if (size == 0u)
        return 0u;

    SpinLock_lock(ImageArena_lockPtr(arena));
    bytes maxB = ImageArena_maxBytesField(arena);
    if (maxB != 0u && ImageArena_usedBytesField(arena) + size > maxB) {
        recordArenaExhaustion(arena, size);
        SpinLock_unlock(ImageArena_lockPtr(arena));
        return 0u;
    }
    ImageFreeBlock *prev = nullptr;
    ImageFreeBlock *curr = ImageArena_freeHead(arena);
    while (curr != nullptr) {
        vpointer cand = ImageFreeBlock_addr(curr);
        bytes candLen = ImageFreeBlock_length(curr);
        if ((cand & (align - 1u)) == 0u && candLen >= size) {
            if (prev == nullptr)
                ImageArena_setFreeHead(arena, ImageFreeBlock_next(curr));
            else
                ImageFreeBlock_setNext(prev, ImageFreeBlock_next(curr));

            MemoryArena_free(Graphvex_arena(), curr);
            if (!trackLiveBlock(arena, cand, candLen)) {
                recordArenaExhaustion(arena, size);
                SpinLock_unlock(ImageArena_lockPtr(arena));
                return 0u;
            }
            ImageArena_setUsedBytesField(arena, ImageArena_usedBytesField(arena) + candLen);
            SpinLock_unlock(ImageArena_lockPtr(arena));
            return cand;
        }
        prev = curr;
        curr = ImageFreeBlock_next(curr);
    }
    ImagePool *pool = ImageArena_poolField(arena);
    SpinLock_lock(ImagePool_lockPtr(pool));
    ImageSlabRecord *slabs = ImagePool_slabs(pool);
    bytes slabCount = ImagePool_slabCountField(pool);
    for (bytes i = 0u; i < slabCount; ++i) {
        if (slabs[i].inUse && slabs[i].owner == arena) {
            vpointer base = slabs[i].info.gpuBase;
            vpointer aligned = (vpointer) alignUp((bytes) (base + slabs[i].cursor), align);
            if (aligned >= base) {
                bytes nextOff = (bytes) (aligned - base);
                if (nextOff <= slabs[i].info.length && size <= slabs[i].info.length - nextOff) {
                    ImagePool_prepareSlabForUse(&slabs[i]);
                    slabs[i].cursor = nextOff + size;
                    slabs[i].liveBytes += size;
                    SpinLock_unlock(ImagePool_lockPtr(pool));
                    if (!trackLiveBlock(arena, aligned, size)) {
                        recordArenaExhaustion(arena, size);
                        SpinLock_unlock(ImageArena_lockPtr(arena));
                        return 0u;
                    }
                    ImageArena_setUsedBytesField(arena, ImageArena_usedBytesField(arena) + size);
                    SpinLock_unlock(ImageArena_lockPtr(arena));
                    return aligned;
                }
            }
        }
    }
    SpinLock_unlock(ImagePool_lockPtr(pool));
    bytes minSlab = size + align;
    ImageSlabRecord *slab = ImagePool_acquireSlab(pool, arena, ImageArena_domainField(arena), minSlab);
    if (slab == nullptr) {
        recordArenaExhaustion(arena, size);
        SpinLock_unlock(ImageArena_lockPtr(arena));
        return 0u;
    }
    SpinLock_lock(ImagePool_lockPtr(pool));
    ImageSlabInfo info = ImageSlabRecord_info(slab);
    vpointer base = info.gpuBase;
    vpointer aligned = (vpointer) alignUp((bytes) base, align);
    bytes nextOff = (bytes) (aligned - base);
    if (nextOff > info.length || size > info.length - nextOff) {
        SpinLock_unlock(ImagePool_lockPtr(pool));
        recordArenaExhaustion(arena, size);
        SpinLock_unlock(ImageArena_lockPtr(arena));
        return 0u;
    }
    ImageSlabRecord_setCursor(slab, nextOff + size);
    ImageSlabRecord_setLiveBytes(slab, ImageSlabRecord_liveBytes(slab) + size);
    bytes slabLen = info.length;
    SpinLock_unlock(ImagePool_lockPtr(pool));
    ImageArena_setCapacityBytesField(arena, ImageArena_capacityBytesField(arena) + slabLen);
    if (!trackLiveBlock(arena, aligned, size)) {
        recordArenaExhaustion(arena, size);
        SpinLock_unlock(ImageArena_lockPtr(arena));
        return 0u;
    }
    ImageArena_setUsedBytesField(arena, ImageArena_usedBytesField(arena) + size);
    SpinLock_unlock(ImageArena_lockPtr(arena));
    return aligned;
}

fn ImageArena_free(ImageArena *arena, vpointer addr)
{
    if (arena == nullptr || addr == 0u)
        return;

    SpinLock_lock(ImageArena_lockPtr(arena));
    ImageLiveBlock *prev = nullptr;
    ImageLiveBlock *curr = ImageArena_liveHead(arena);
    while (curr != nullptr) {
        if (ImageLiveBlock_addr(curr) == addr) {
            bytes len = ImageLiveBlock_length(curr);
            if (prev == nullptr)
                ImageArena_setLiveHead(arena, ImageLiveBlock_next(curr));
            else
                ImageLiveBlock_setNext(prev, ImageLiveBlock_next(curr));

            MemoryArena_free(Graphvex_arena(), curr);
            ImageFreeBlock *f = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *f);
            if (f != nullptr) {
                ImageFreeBlock_setAddr(f, addr);
                ImageFreeBlock_setLength(f, len);
                ImageFreeBlock_setNext(f, ImageArena_freeHead(arena));
                ImageArena_setFreeHead(arena, f);
            }
            if (ImageArena_usedBytesField(arena) >= len)
                ImageArena_setUsedBytesField(arena, ImageArena_usedBytesField(arena) - len);

            SpinLock_unlock(ImageArena_lockPtr(arena));
            return;
        }
        prev = curr;
        curr = ImageLiveBlock_next(curr);
    }
    SpinLock_unlock(ImageArena_lockPtr(arena));
}

fn ImageArena_reset(ImageArena *arena)
{
    if (arena == nullptr)
        return;

    SpinLock_lock(ImageArena_lockPtr(arena));
    clearBookkeeping(arena);
    ImageArena_setUsedBytesField(arena, 0u);
    ImagePool *pool = ImageArena_poolField(arena);
    SpinLock_unlock(ImageArena_lockPtr(arena));
    bytes remCap = ImagePool_resetArenaSlabs(pool, arena, false);
    SpinLock_lock(ImageArena_lockPtr(arena));
    ImageArena_setCapacityBytesField(arena, remCap);
    SpinLock_unlock(ImageArena_lockPtr(arena));
}

fn ImageArena_trim(ImageArena *arena)
{
    if (arena == nullptr)
        return;

    SpinLock_lock(ImageArena_lockPtr(arena));
    clearBookkeeping(arena);
    ImageArena_setUsedBytesField(arena, 0u);
    ImagePool *pool = ImageArena_poolField(arena);
    SpinLock_unlock(ImageArena_lockPtr(arena));
    bytes remCap = ImagePool_resetArenaSlabs(pool, arena, true);
    SpinLock_lock(ImageArena_lockPtr(arena));
    ImageArena_setCapacityBytesField(arena, remCap);
    SpinLock_unlock(ImageArena_lockPtr(arena));
}

ImagePool *ImageArena_pool(const ImageArena *arena)
{
    if (arena == nullptr)
        return nullptr;

    return ImageArena_poolField(arena);
}

ImageMemoryDomain ImageArena_domain(const ImageArena *arena)
{
    if (arena == nullptr)
        return IMAGE_MEMORY_SHARED;

    return ImageArena_domainField(arena);
}

bytes ImageArena_usedBytes(const ImageArena *arena)
{
    if (arena == nullptr)
        return 0u;

    return ImageArena_usedBytesField(arena);
}

bytes ImageArena_capacity(const ImageArena *arena)
{
    if (arena == nullptr)
        return 0u;

    return ImageArena_capacityBytesField(arena);
}

bytes ImageArena_maxBytes(const ImageArena *arena)
{
    if (arena == nullptr)
        return 0u;

    return ImageArena_maxBytesField(arena);
}

u64 ImageArena_exhaustionCount(const ImageArena *arena)
{
    if (arena == nullptr)
        return 0u;

    return ImageArena_exhaustionCountField(arena);
}
