#ifndef GRAPHVEX_VIO_INTERNAL_H
#define GRAPHVEX_VIO_INTERNAL_H

#include "vio/vio.h"
#include "nio/graphvex_memory.h"
#include "re/atomic/atomic.h"

typedef struct VideoSlabRecord {
    VideoSlabInfo info;
    VideoArena *owner;
    bytes cursor;
    bytes liveBytes;
    bool inUse;
} VideoSlabRecord;

typedef struct VideoFreeBlock {
    vpointer addr;
    bytes length;
    struct VideoFreeBlock *next;
} VideoFreeBlock;

typedef struct VideoLiveBlock {
    vpointer addr;
    bytes length;
    struct VideoLiveBlock *next;
} VideoLiveBlock;

struct VideoPool {
    VideoPoolDriver driver;
    SpinLock lock;
    bytes slabBytes;
    VideoSlabRecord *slabs;
    bytes slabCount;
    bytes slabCapacity;
    u64 exhaustionCount;
    bool exhaustionReported;
};

struct VideoArena {
    VideoPool *pool;
    VideoMemoryDomain domain;
    SpinLock lock;
    bytes maxBytes;
    bytes capacityBytes;
    bytes usedBytes;
    VideoFreeBlock *freeHead;
    VideoLiveBlock *liveHead;
    u64 exhaustionCount;
    bool exhaustionReported;
};

;;GETTER
static inline VideoPoolDriver VideoPool_driver(const VideoPool *p)
{
    return (*p).driver;
}
;;SETTER
static inline fn VideoPool_setDriver(VideoPool *p, VideoPoolDriver d)
{
    (*p).driver = d;
}
;;GETTER
static inline SpinLock *VideoPool_lockPtr(VideoPool *p)
{
    return &(*p).lock;
}
;;GETTER
static inline bytes VideoPool_slabBytesField(const VideoPool *p)
{
    return (*p).slabBytes;
}
;;SETTER
static inline fn VideoPool_setSlabBytesField(VideoPool *p, bytes n)
{
    (*p).slabBytes = n;
}
;;GETTER
static inline VideoSlabRecord *VideoPool_slabs(const VideoPool *p)
{
    return (*p).slabs;
}
;;SETTER
static inline fn VideoPool_setSlabs(VideoPool *p, VideoSlabRecord *s)
{
    (*p).slabs = s;
}
;;GETTER
static inline bytes VideoPool_slabCountField(const VideoPool *p)
{
    return (*p).slabCount;
}
;;SETTER
static inline fn VideoPool_setSlabCountField(VideoPool *p, bytes n)
{
    (*p).slabCount = n;
}
;;GETTER
static inline bytes VideoPool_slabCapacity(const VideoPool *p)
{
    return (*p).slabCapacity;
}
;;SETTER
static inline fn VideoPool_setSlabCapacity(VideoPool *p, bytes n)
{
    (*p).slabCapacity = n;
}
;;GETTER
static inline u64 VideoPool_exhaustionCountField(const VideoPool *p)
{
    return (*p).exhaustionCount;
}
;;SETTER
static inline fn VideoPool_setExhaustionCountField(VideoPool *p, u64 n)
{
    (*p).exhaustionCount = n;
}
;;GETTER
static inline bool VideoPool_exhaustionReported(const VideoPool *p)
{
    return (*p).exhaustionReported;
}
;;SETTER
static inline fn VideoPool_setExhaustionReported(VideoPool *p, bool v)
{
    (*p).exhaustionReported = v;
}

;;GETTER
static inline VideoPool *VideoArena_poolField(const VideoArena *a)
{
    return (*a).pool;
}
;;SETTER
static inline fn VideoArena_setPoolField(VideoArena *a, VideoPool *p)
{
    (*a).pool = p;
}
;;GETTER
static inline VideoMemoryDomain VideoArena_domainField(const VideoArena *a)
{
    return (*a).domain;
}
;;SETTER
static inline fn VideoArena_setDomainField(VideoArena *a, VideoMemoryDomain d)
{
    (*a).domain = d;
}
;;GETTER
static inline SpinLock *VideoArena_lockPtr(VideoArena *a)
{
    return &(*a).lock;
}
;;GETTER
static inline bytes VideoArena_maxBytesField(const VideoArena *a)
{
    return (*a).maxBytes;
}
;;SETTER
static inline fn VideoArena_setMaxBytesField(VideoArena *a, bytes n)
{
    (*a).maxBytes = n;
}
;;GETTER
static inline bytes VideoArena_capacityBytesField(const VideoArena *a)
{
    return (*a).capacityBytes;
}
;;SETTER
static inline fn VideoArena_setCapacityBytesField(VideoArena *a, bytes n)
{
    (*a).capacityBytes = n;
}
;;GETTER
static inline bytes VideoArena_usedBytesField(const VideoArena *a)
{
    return (*a).usedBytes;
}
;;SETTER
static inline fn VideoArena_setUsedBytesField(VideoArena *a, bytes n)
{
    (*a).usedBytes = n;
}
;;GETTER
static inline VideoFreeBlock *VideoArena_freeHead(const VideoArena *a)
{
    return (*a).freeHead;
}
;;SETTER
static inline fn VideoArena_setFreeHead(VideoArena *a, VideoFreeBlock *n)
{
    (*a).freeHead = n;
}
;;GETTER
static inline VideoLiveBlock *VideoArena_liveHead(const VideoArena *a)
{
    return (*a).liveHead;
}
;;SETTER
static inline fn VideoArena_setLiveHead(VideoArena *a, VideoLiveBlock *n)
{
    (*a).liveHead = n;
}
;;GETTER
static inline u64 VideoArena_exhaustionCountField(const VideoArena *a)
{
    return (*a).exhaustionCount;
}
;;SETTER
static inline fn VideoArena_setExhaustionCountField(VideoArena *a, u64 n)
{
    (*a).exhaustionCount = n;
}
;;GETTER
static inline bool VideoArena_exhaustionReported(const VideoArena *a)
{
    return (*a).exhaustionReported;
}
;;SETTER
static inline fn VideoArena_setExhaustionReported(VideoArena *a, bool v)
{
    (*a).exhaustionReported = v;
}

;;GETTER
static inline VideoFreeBlock *VideoFreeBlock_next(const VideoFreeBlock *b)
{
    return (*b).next;
}
;;SETTER
static inline fn VideoFreeBlock_setNext(VideoFreeBlock *b, VideoFreeBlock *n)
{
    (*b).next = n;
}
;;GETTER
static inline vpointer VideoFreeBlock_addr(const VideoFreeBlock *b)
{
    return (*b).addr;
}
;;SETTER
static inline fn VideoFreeBlock_setAddr(VideoFreeBlock *b, vpointer a)
{
    (*b).addr = a;
}
;;GETTER
static inline bytes VideoFreeBlock_length(const VideoFreeBlock *b)
{
    return (*b).length;
}
;;SETTER
static inline fn VideoFreeBlock_setLength(VideoFreeBlock *b, bytes l)
{
    (*b).length = l;
}

;;GETTER
static inline VideoLiveBlock *VideoLiveBlock_next(const VideoLiveBlock *b)
{
    return (*b).next;
}
;;SETTER
static inline fn VideoLiveBlock_setNext(VideoLiveBlock *b, VideoLiveBlock *n)
{
    (*b).next = n;
}
;;GETTER
static inline vpointer VideoLiveBlock_addr(const VideoLiveBlock *b)
{
    return (*b).addr;
}
;;SETTER
static inline fn VideoLiveBlock_setAddr(VideoLiveBlock *b, vpointer a)
{
    (*b).addr = a;
}
;;GETTER
static inline bytes VideoLiveBlock_length(const VideoLiveBlock *b)
{
    return (*b).length;
}
;;SETTER
static inline fn VideoLiveBlock_setLength(VideoLiveBlock *b, bytes l)
{
    (*b).length = l;
}

;;GETTER
static inline VideoSlabInfo VideoSlabRecord_info(const VideoSlabRecord *r)
{
    return (*r).info;
}
;;GETTER
static inline bytes VideoSlabRecord_liveBytes(const VideoSlabRecord *r)
{
    return (*r).liveBytes;
}
;;SETTER
static inline fn VideoSlabRecord_setCursor(VideoSlabRecord *r, bytes c)
{
    (*r).cursor = c;
}
;;SETTER
static inline fn VideoSlabRecord_setLiveBytes(VideoSlabRecord *r, bytes n)
{
    (*r).liveBytes = n;
}

;;GETTER
static inline any VideoSlabInfo_driverHandle(const VideoSlabInfo *s)
{
    return (*s).driverHandle;
}
;;GETTER
static inline bytes VideoSlabInfo_length(const VideoSlabInfo *s)
{
    return (*s).length;
}

;;GETTER
static inline bool VideoPoolDriver_hasAlloc(const VideoPoolDriver *d)
{
    return (*d).allocSlab != nullptr && (*d).freeSlab != nullptr;
}

VideoSlabRecord *VideoPool_acquireSlab(VideoPool *pool, VideoArena *owner, VideoMemoryDomain domain, bytes minBytes);
fn VideoPool_releaseArenaSlabs(VideoPool *pool, VideoArena *owner);
fn VideoPool_resetArenaSlabs(VideoPool *pool, VideoArena *owner);
bool VideoPool_findByGpu(const VideoPool *pool, vpointer addr, bytes length, VideoSlabInfo *outInfo, bytes *outOffset);
bool VideoPool_findByCpu(const VideoPool *pool, pointer cpuAddr, VideoSlabInfo *outInfo, bytes *outOffset);

#endif
