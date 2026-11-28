#ifndef GRAPHVEX_VIO_INTERNAL_H
#define GRAPHVEX_VIO_INTERNAL_H

#include "vio/vio.h"
#include "nio/graphvex_memory.h"
#include "re/atomic/atomic.h"

/**
 * Per-slab ownership and bump cursor record inside an ImagePool.
 *
 * ImageSlabInfo info - driver slab metadata
 * ImageArena *owner  - current owning arena (nullptr when free)
 * bytes cursor       - bump offset within slab
 * bytes liveBytes    - live allocated bytes in slab
 * u32 idleResets     - consecutive resets without allocations
 * bool inUse         - true while assigned to an arena
 * bool reusable      - true when OS pages are advised reusable
 */
typedef struct ImageSlabRecord {
    ImageSlabInfo info;
    ImageArena *owner;
    bytes cursor;
    bytes liveBytes;
    u32 idleResets;
    bool inUse;
    bool reusable;
} ImageSlabRecord;

/**
 * Freed sub-allocation node stored on an ImageArena free list.
 *
 * vpointer addr             - GPU virtual address of free block
 * bytes length              - byte length of free block
 * struct ImageFreeBlock *next - next free node
 */
typedef struct ImageFreeBlock {
    vpointer addr;
    bytes length;
    struct ImageFreeBlock *next;
} ImageFreeBlock;

/**
 * Active sub-allocation node stored on an ImageArena live list.
 *
 * vpointer addr             - GPU virtual address of live block
 * bytes length              - byte length of live block
 * struct ImageLiveBlock *next - next live node
 */
typedef struct ImageLiveBlock {
    vpointer addr;
    bytes length;
    struct ImageLiveBlock *next;
} ImageLiveBlock;

/**
 * Internal state for ImagePool.
 *
 * ImagePoolDriver driver  - backend slab driver vtable
 * SpinLock lock           - protects slab table mutations
 * bytes slabBytes         - default slab byte size
 * ImageSlabRecord *slabs  - growable slab array
 * bytes slabCount         - active slab count
 * bytes slabCapacity      - allocated slab array capacity
 * u64 exhaustionCount     - total slab allocation refusals
 * bool exhaustionReported - one-shot THROW guard
 */
struct ImagePool {
    ImagePoolDriver driver;
    SpinLock lock;
    bytes slabBytes;
    ImageSlabRecord *slabs;
    bytes slabCount;
    bytes slabCapacity;
    u64 exhaustionCount;
    bool exhaustionReported;
};

/**
 * Internal state for ImageArena.
 *
 * ImagePool *pool         - parent ImagePool
 * ImageMemoryDomain domain - residency domain
 * SpinLock lock           - protects arena lists and counters
 * bytes maxBytes          - optional hard byte budget (0 = unbounded)
 * bytes capacityBytes     - sum of owned slab capacities
 * bytes usedBytes         - current live byte usage
 * ImageFreeBlock *freeHead - recycled block list
 * ImageLiveBlock *liveHead - live block list
 * u64 exhaustionCount     - total allocation refusals
 * bool exhaustionReported - one-shot THROW guard
 */
struct ImageArena {
    ImagePool *pool;
    ImageMemoryDomain domain;
    SpinLock lock;
    bytes maxBytes;
    bytes capacityBytes;
    bytes usedBytes;
    ImageFreeBlock *freeHead;
    ImageLiveBlock *liveHead;
    u64 exhaustionCount;
    bool exhaustionReported;
};

;;GETTER
static inline ImagePoolDriver ImagePool_driver(const ImagePool *p)
{
    return (*p).driver;
}

;;SETTER
static inline fn ImagePool_setDriver(ImagePool *p, ImagePoolDriver d)
{
    (*p).driver = d;
}

;;GETTER
static inline SpinLock *ImagePool_lockPtr(ImagePool *p)
{
    return &(*p).lock;
}

;;GETTER
static inline bytes ImagePool_slabBytesField(const ImagePool *p)
{
    return (*p).slabBytes;
}

;;SETTER
static inline fn ImagePool_setSlabBytesField(ImagePool *p, bytes n)
{
    (*p).slabBytes = n;
}

;;GETTER
static inline ImageSlabRecord *ImagePool_slabs(const ImagePool *p)
{
    return (*p).slabs;
}

;;SETTER
static inline fn ImagePool_setSlabs(ImagePool *p, ImageSlabRecord *s)
{
    (*p).slabs = s;
}

;;GETTER
static inline bytes ImagePool_slabCountField(const ImagePool *p)
{
    return (*p).slabCount;
}

;;SETTER
static inline fn ImagePool_setSlabCountField(ImagePool *p, bytes n)
{
    (*p).slabCount = n;
}

;;GETTER
static inline bytes ImagePool_slabCapacity(const ImagePool *p)
{
    return (*p).slabCapacity;
}

;;SETTER
static inline fn ImagePool_setSlabCapacity(ImagePool *p, bytes n)
{
    (*p).slabCapacity = n;
}

;;GETTER
static inline u64 ImagePool_exhaustionCountField(const ImagePool *p)
{
    return (*p).exhaustionCount;
}

;;SETTER
static inline fn ImagePool_setExhaustionCountField(ImagePool *p, u64 n)
{
    (*p).exhaustionCount = n;
}

;;GETTER
static inline bool ImagePool_exhaustionReported(const ImagePool *p)
{
    return (*p).exhaustionReported;
}

;;SETTER
static inline fn ImagePool_setExhaustionReported(ImagePool *p, bool v)
{
    (*p).exhaustionReported = v;
}

;;GETTER
static inline ImagePool *ImageArena_poolField(const ImageArena *a)
{
    return (*a).pool;
}

;;SETTER
static inline fn ImageArena_setPoolField(ImageArena *a, ImagePool *p)
{
    (*a).pool = p;
}

;;GETTER
static inline ImageMemoryDomain ImageArena_domainField(const ImageArena *a)
{
    return (*a).domain;
}

;;SETTER
static inline fn ImageArena_setDomainField(ImageArena *a, ImageMemoryDomain d)
{
    (*a).domain = d;
}

;;GETTER
static inline SpinLock *ImageArena_lockPtr(ImageArena *a)
{
    return &(*a).lock;
}

;;GETTER
static inline bytes ImageArena_maxBytesField(const ImageArena *a)
{
    return (*a).maxBytes;
}

;;SETTER
static inline fn ImageArena_setMaxBytesField(ImageArena *a, bytes n)
{
    (*a).maxBytes = n;
}

;;GETTER
static inline bytes ImageArena_capacityBytesField(const ImageArena *a)
{
    return (*a).capacityBytes;
}

;;SETTER
static inline fn ImageArena_setCapacityBytesField(ImageArena *a, bytes n)
{
    (*a).capacityBytes = n;
}

;;GETTER
static inline bytes ImageArena_usedBytesField(const ImageArena *a)
{
    return (*a).usedBytes;
}

;;SETTER
static inline fn ImageArena_setUsedBytesField(ImageArena *a, bytes n)
{
    (*a).usedBytes = n;
}

;;GETTER
static inline ImageFreeBlock *ImageArena_freeHead(const ImageArena *a)
{
    return (*a).freeHead;
}

;;SETTER
static inline fn ImageArena_setFreeHead(ImageArena *a, ImageFreeBlock *n)
{
    (*a).freeHead = n;
}

;;GETTER
static inline ImageLiveBlock *ImageArena_liveHead(const ImageArena *a)
{
    return (*a).liveHead;
}

;;SETTER
static inline fn ImageArena_setLiveHead(ImageArena *a, ImageLiveBlock *n)
{
    (*a).liveHead = n;
}

;;GETTER
static inline u64 ImageArena_exhaustionCountField(const ImageArena *a)
{
    return (*a).exhaustionCount;
}

;;SETTER
static inline fn ImageArena_setExhaustionCountField(ImageArena *a, u64 n)
{
    (*a).exhaustionCount = n;
}

;;GETTER
static inline bool ImageArena_exhaustionReported(const ImageArena *a)
{
    return (*a).exhaustionReported;
}

;;SETTER
static inline fn ImageArena_setExhaustionReported(ImageArena *a, bool v)
{
    (*a).exhaustionReported = v;
}

;;GETTER
static inline ImageFreeBlock *ImageFreeBlock_next(const ImageFreeBlock *b)
{
    return (*b).next;
}

;;SETTER
static inline fn ImageFreeBlock_setNext(ImageFreeBlock *b, ImageFreeBlock *n)
{
    (*b).next = n;
}

;;GETTER
static inline vpointer ImageFreeBlock_addr(const ImageFreeBlock *b)
{
    return (*b).addr;
}

;;SETTER
static inline fn ImageFreeBlock_setAddr(ImageFreeBlock *b, vpointer a)
{
    (*b).addr = a;
}

;;GETTER
static inline bytes ImageFreeBlock_length(const ImageFreeBlock *b)
{
    return (*b).length;
}

;;SETTER
static inline fn ImageFreeBlock_setLength(ImageFreeBlock *b, bytes l)
{
    (*b).length = l;
}

;;GETTER
static inline ImageLiveBlock *ImageLiveBlock_next(const ImageLiveBlock *b)
{
    return (*b).next;
}

;;SETTER
static inline fn ImageLiveBlock_setNext(ImageLiveBlock *b, ImageLiveBlock *n)
{
    (*b).next = n;
}

;;GETTER
static inline vpointer ImageLiveBlock_addr(const ImageLiveBlock *b)
{
    return (*b).addr;
}

;;SETTER
static inline fn ImageLiveBlock_setAddr(ImageLiveBlock *b, vpointer a)
{
    (*b).addr = a;
}

;;GETTER
static inline bytes ImageLiveBlock_length(const ImageLiveBlock *b)
{
    return (*b).length;
}

;;SETTER
static inline fn ImageLiveBlock_setLength(ImageLiveBlock *b, bytes l)
{
    (*b).length = l;
}

;;GETTER
static inline ImageSlabInfo ImageSlabRecord_info(const ImageSlabRecord *r)
{
    return (*r).info;
}

;;GETTER
static inline bytes ImageSlabRecord_liveBytes(const ImageSlabRecord *r)
{
    return (*r).liveBytes;
}

;;SETTER
static inline fn ImageSlabRecord_setCursor(ImageSlabRecord *r, bytes c)
{
    (*r).cursor = c;
}

;;SETTER
static inline fn ImageSlabRecord_setLiveBytes(ImageSlabRecord *r, bytes n)
{
    (*r).liveBytes = n;
}

;;GETTER
static inline any ImageSlabInfo_driverHandle(const ImageSlabInfo *s)
{
    return (*s).driverHandle;
}

;;GETTER
static inline bytes ImageSlabInfo_length(const ImageSlabInfo *s)
{
    return (*s).length;
}

;;GETTER
static inline u32 ImageSlabRecord_idleResets(const ImageSlabRecord *s)
{
    if (!s)
        return 0u;

    return (*s).idleResets;
}

;;SETTER
static inline fn ImageSlabRecord_setIdleResets(ImageSlabRecord *s, u32 n)
{
    if (!s)
        return;

    (*s).idleResets = n;
}

;;GETTER
static inline bool ImageSlabRecord_reusable(const ImageSlabRecord *s)
{
    if (!s)
        return false;

    return (*s).reusable;
}

;;SETTER
static inline fn ImageSlabRecord_setReusable(ImageSlabRecord *s, bool v)
{
    if (!s)
        return;

    (*s).reusable = v;
}

;;GETTER
static inline bool ImagePoolDriver_hasAlloc(const ImagePoolDriver *d)
{
    return (*d).allocSlab != nullptr && (*d).freeSlab != nullptr;
}

ImageSlabRecord *ImagePool_acquireSlab(ImagePool *pool, ImageArena *owner, ImageMemoryDomain domain, bytes minBytes);
fn ImagePool_prepareSlabForUse(ImageSlabRecord *slab);
fn ImagePool_releaseArenaSlabs(ImagePool *pool, ImageArena *owner);
bytes ImagePool_resetArenaSlabs(ImagePool *pool, ImageArena *owner, bool aggressive);
bool ImagePool_findByGpu(const ImagePool *pool, vpointer addr, bytes length, ImageSlabInfo *outInfo, bytes *outOffset);
bool ImagePool_findByCpu(const ImagePool *pool, pointer cpuAddr, ImageSlabInfo *outInfo, bytes *outOffset);

#endif
