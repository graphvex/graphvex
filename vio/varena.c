#include "vex.h"
#include "vio/vio_internal.h"

;;DEFINITION
/** VideoArena is a per-subsystem virtual GPU arena carved from a VideoPool.
 * It sub-allocates vpointer ranges with power-of-two alignment (floor 32B,
 * default 256B), recycles freed blocks on a free list, supports O(1) reset for
 * per-frame transient GPU scratch, and enforces optional hard byte budgets. */

;;OVERVIEW
/** CLASS: VideoArena (vio/varena.c)
 * STRUCT FIELDS: parent VideoPool, VideoMemoryDomain, SpinLock, maxBytes,
 * capacityBytes, usedBytes, freeHead, liveHead, exhaustion counter.
 * PRIVATE HELPERS: alignUp, isPow2, recordArenaExhaustion, trackLiveBlock.
 * FUNCTION REGISTRY: create, destroy, alloc, free, reset, accessors.
 * CONTRACT: returns 0 (null vpointer) on exhaustion or invalid size; zero
 * libc allocations (metadata lives in Graphvex_arena()). */

static bool isPow2(bytes n) {
    return n != 0u && (n & (n - 1u)) == 0u;
}

static bytes alignUp(bytes value, bytes alignment) {
    bytes mask = alignment - 1u;
    if (value > SIZE_MAX - mask)
        return 0u;
    return (value + mask) & ~mask;
}

static fn recordArenaExhaustion(VideoArena *arena, bytes requested) {
    if (VideoArena_exhaustionCountField(arena) != UINT64_MAX)
        VideoArena_setExhaustionCountField(arena, VideoArena_exhaustionCountField(arena) + 1u);
    if (!VideoArena_exhaustionReported(arena)) {
        THROW("VideoArena: allocation refused for %zu bytes", requested);
        VideoArena_setExhaustionReported(arena, true);
    }
}

static bool trackLiveBlock(VideoArena *arena, vpointer addr, bytes length) {
    VideoLiveBlock *node = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *node);
    if (node == nullptr)
        return false;
    VideoLiveBlock_setAddr(node, addr);
    VideoLiveBlock_setLength(node, length);
    VideoLiveBlock_setNext(node, VideoArena_liveHead(arena));
    VideoArena_setLiveHead(arena, node);
    return true;
}

static fn clearBookkeeping(VideoArena *arena) {
    VideoFreeBlock *f = VideoArena_freeHead(arena);
    while (f != nullptr) {
        VideoFreeBlock *next = VideoFreeBlock_next(f);
        MemoryArena_free(Graphvex_arena(), f);
        f = next;
    }
    VideoArena_setFreeHead(arena, nullptr);
    VideoLiveBlock *l = VideoArena_liveHead(arena);
    while (l != nullptr) {
        VideoLiveBlock *next = VideoLiveBlock_next(l);
        MemoryArena_free(Graphvex_arena(), l);
        l = next;
    }
    VideoArena_setLiveHead(arena, nullptr);
}

;;CONSTRUCTOR
VideoArena *VideoArena_3(VideoPool *pool, VideoMemoryDomain domain, bytes maxBytes)
{
    if (pool == nullptr) {
        THROW("VideoArena: rejected null VideoPool");
        return nullptr;
    }
    VideoArena *arena = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *arena);
    if (arena == nullptr) {
        THROW("VideoArena: failed to allocate arena metadata");
        return nullptr;
    }
    memset(arena, 0, sizeof *arena);
    VideoArena_setPoolField(arena, pool);
    VideoArena_setDomainField(arena, domain);
    VideoArena_setMaxBytesField(arena, maxBytes);
    return arena;
}

fn VideoArena_destroy(VideoArena *arena) {
    if (arena == nullptr)
        return;
    SpinLock_lock(VideoArena_lockPtr(arena));
    clearBookkeeping(arena);
    VideoPool *pool = VideoArena_poolField(arena);
    VideoArena_setUsedBytesField(arena, 0u);
    VideoArena_setCapacityBytesField(arena, 0u);
    SpinLock_unlock(VideoArena_lockPtr(arena));
    VideoPool_releaseArenaSlabs(pool, arena);
    MemoryArena_free(Graphvex_arena(), arena);
}

vpointer VideoArena_alloc(VideoArena *arena, bytes numBytes, bytes alignment) {
    if (arena == nullptr || numBytes == 0u)
        return 0u;
    bytes align = alignment == 0u ? VIDEO_MEMORY_DEFAULT_ALIGN : alignment;
    if (align < VIDEO_MEMORY_ALIGN_FLOOR)
        align = VIDEO_MEMORY_ALIGN_FLOOR;
    if (!isPow2(align)) {
        THROW("VideoArena: alignment must be power of two, got %zu", align);
        return 0u;
    }
    bytes size = alignUp(numBytes, VIDEO_MEMORY_ALIGN_FLOOR);
    if (size == 0u)
        return 0u;
    SpinLock_lock(VideoArena_lockPtr(arena));
    bytes maxB = VideoArena_maxBytesField(arena);
    if (maxB != 0u && VideoArena_usedBytesField(arena) + size > maxB) {
        recordArenaExhaustion(arena, size);
        SpinLock_unlock(VideoArena_lockPtr(arena));
        return 0u;
    }
    VideoFreeBlock *prev = nullptr;
    VideoFreeBlock *curr = VideoArena_freeHead(arena);
    while (curr != nullptr) {
        vpointer cand = VideoFreeBlock_addr(curr);
        bytes candLen = VideoFreeBlock_length(curr);
        if ((cand & (align - 1u)) == 0u && candLen >= size) {
            if (prev == nullptr)
                VideoArena_setFreeHead(arena, VideoFreeBlock_next(curr));
            else
                VideoFreeBlock_setNext(prev, VideoFreeBlock_next(curr));
            MemoryArena_free(Graphvex_arena(), curr);
            if (!trackLiveBlock(arena, cand, candLen)) {
                recordArenaExhaustion(arena, size);
                SpinLock_unlock(VideoArena_lockPtr(arena));
                return 0u;
            }
            VideoArena_setUsedBytesField(arena, VideoArena_usedBytesField(arena) + candLen);
            SpinLock_unlock(VideoArena_lockPtr(arena));
            return cand;
        }
        prev = curr;
        curr = VideoFreeBlock_next(curr);
    }
    VideoPool *pool = VideoArena_poolField(arena);
    SpinLock_lock(VideoPool_lockPtr(pool));
    VideoSlabRecord *slabs = VideoPool_slabs(pool);
    bytes slabCount = VideoPool_slabCountField(pool);
    for (bytes i = 0u; i < slabCount; ++i) {
        if (slabs[i].inUse && slabs[i].owner == arena) {
            vpointer base = slabs[i].info.gpuBase;
            vpointer aligned = (vpointer) alignUp((bytes) (base + slabs[i].cursor), align);
            if (aligned >= base) {
                bytes nextOff = (bytes) (aligned - base);
                if (nextOff <= slabs[i].info.length && size <= slabs[i].info.length - nextOff) {
                    VideoPool_prepareSlabForUse(&slabs[i]);
                    slabs[i].cursor = nextOff + size;
                    slabs[i].liveBytes += size;
                    SpinLock_unlock(VideoPool_lockPtr(pool));
                    if (!trackLiveBlock(arena, aligned, size)) {
                        recordArenaExhaustion(arena, size);
                        SpinLock_unlock(VideoArena_lockPtr(arena));
                        return 0u;
                    }
                    VideoArena_setUsedBytesField(arena, VideoArena_usedBytesField(arena) + size);
                    SpinLock_unlock(VideoArena_lockPtr(arena));
                    return aligned;
                }
            }
        }
    }
    SpinLock_unlock(VideoPool_lockPtr(pool));
    bytes minSlab = size + align;
    VideoSlabRecord *slab = VideoPool_acquireSlab(pool, arena, VideoArena_domainField(arena), minSlab);
    if (slab == nullptr) {
        recordArenaExhaustion(arena, size);
        SpinLock_unlock(VideoArena_lockPtr(arena));
        return 0u;
    }
    SpinLock_lock(VideoPool_lockPtr(pool));
    VideoSlabInfo info = VideoSlabRecord_info(slab);
    vpointer base = info.gpuBase;
    vpointer aligned = (vpointer) alignUp((bytes) base, align);
    bytes nextOff = (bytes) (aligned - base);
    if (nextOff > info.length || size > info.length - nextOff) {
        SpinLock_unlock(VideoPool_lockPtr(pool));
        recordArenaExhaustion(arena, size);
        SpinLock_unlock(VideoArena_lockPtr(arena));
        return 0u;
    }
    VideoSlabRecord_setCursor(slab, nextOff + size);
    VideoSlabRecord_setLiveBytes(slab, VideoSlabRecord_liveBytes(slab) + size);
    bytes slabLen = info.length;
    SpinLock_unlock(VideoPool_lockPtr(pool));
    VideoArena_setCapacityBytesField(arena, VideoArena_capacityBytesField(arena) + slabLen);
    if (!trackLiveBlock(arena, aligned, size)) {
        recordArenaExhaustion(arena, size);
        SpinLock_unlock(VideoArena_lockPtr(arena));
        return 0u;
    }
    VideoArena_setUsedBytesField(arena, VideoArena_usedBytesField(arena) + size);
    SpinLock_unlock(VideoArena_lockPtr(arena));
    return aligned;
}

fn VideoArena_free(VideoArena *arena, vpointer addr) {
    if (arena == nullptr || addr == 0u)
        return;
    SpinLock_lock(VideoArena_lockPtr(arena));
    VideoLiveBlock *prev = nullptr;
    VideoLiveBlock *curr = VideoArena_liveHead(arena);
    while (curr != nullptr) {
        if (VideoLiveBlock_addr(curr) == addr) {
            bytes len = VideoLiveBlock_length(curr);
            if (prev == nullptr)
                VideoArena_setLiveHead(arena, VideoLiveBlock_next(curr));
            else
                VideoLiveBlock_setNext(prev, VideoLiveBlock_next(curr));
            MemoryArena_free(Graphvex_arena(), curr);
            VideoFreeBlock *f = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *f);
            if (f != nullptr) {
                VideoFreeBlock_setAddr(f, addr);
                VideoFreeBlock_setLength(f, len);
                VideoFreeBlock_setNext(f, VideoArena_freeHead(arena));
                VideoArena_setFreeHead(arena, f);
            }
            if (VideoArena_usedBytesField(arena) >= len)
                VideoArena_setUsedBytesField(arena, VideoArena_usedBytesField(arena) - len);
            SpinLock_unlock(VideoArena_lockPtr(arena));
            return;
        }
        prev = curr;
        curr = VideoLiveBlock_next(curr);
    }
    SpinLock_unlock(VideoArena_lockPtr(arena));
}

fn VideoArena_reset(VideoArena *arena) {
    if (arena == nullptr)
        return;
    SpinLock_lock(VideoArena_lockPtr(arena));
    clearBookkeeping(arena);
    VideoArena_setUsedBytesField(arena, 0u);
    VideoPool *pool = VideoArena_poolField(arena);
    SpinLock_unlock(VideoArena_lockPtr(arena));
    bytes remCap = VideoPool_resetArenaSlabs(pool, arena, false);
    SpinLock_lock(VideoArena_lockPtr(arena));
    VideoArena_setCapacityBytesField(arena, remCap);
    SpinLock_unlock(VideoArena_lockPtr(arena));
}

fn VideoArena_trim(VideoArena *arena) {
    if (arena == nullptr)
        return;
    SpinLock_lock(VideoArena_lockPtr(arena));
    clearBookkeeping(arena);
    VideoArena_setUsedBytesField(arena, 0u);
    VideoPool *pool = VideoArena_poolField(arena);
    SpinLock_unlock(VideoArena_lockPtr(arena));
    bytes remCap = VideoPool_resetArenaSlabs(pool, arena, true);
    SpinLock_lock(VideoArena_lockPtr(arena));
    VideoArena_setCapacityBytesField(arena, remCap);
    SpinLock_unlock(VideoArena_lockPtr(arena));
}

VideoPool *VideoArena_pool(const VideoArena *arena) {
    return arena == nullptr ? nullptr : VideoArena_poolField(arena);
}

VideoMemoryDomain VideoArena_domain(const VideoArena *arena) {
    return arena == nullptr ? VIDEO_MEMORY_SHARED : VideoArena_domainField(arena);
}

bytes VideoArena_usedBytes(const VideoArena *arena) {
    return arena == nullptr ? 0u : VideoArena_usedBytesField(arena);
}

bytes VideoArena_capacity(const VideoArena *arena) {
    return arena == nullptr ? 0u : VideoArena_capacityBytesField(arena);
}

bytes VideoArena_maxBytes(const VideoArena *arena) {
    return arena == nullptr ? 0u : VideoArena_maxBytesField(arena);
}

u64 VideoArena_exhaustionCount(const VideoArena *arena) {
    return arena == nullptr ? 0u : VideoArena_exhaustionCountField(arena);
}
