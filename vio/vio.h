#ifndef GRAPHVEX_VIO_H
#define GRAPHVEX_VIO_H

/**
 * vio/vio.h -- GPU memory substrate for graphvex (parallel to re/nio/).
 *
 * Three-tier GPU memory ownership:
 * VideoPool  (growable raw GPU slabs from Metal/Vulkan/D3D12/wgpu)
 * VideoArena (per-subsystem virtual GPU arena carved from a VideoPool)
 * vpointer   (64-bit GPU virtual address, 0 = null)
 *
 * On Unified Memory (Apple Silicon / UMA), VIDEO_MEMORY_SHARED slabs are backed
 * by 16 KiB page-aligned host memory wrapped zero-copy into the GPU address
 * space, so VideoMemory_toPointer(pool, vptr) returns the direct CPU pointer in
 * O(1) without staging or copying.
 */

#include "vex.h"

#define VIDEO_MEMORY_ALIGN_FLOOR 32u
#define VIDEO_MEMORY_DEFAULT_ALIGN 256u
#define VIDEO_MEMORY_DEFAULT_SLAB_BYTES (1024u * 1024u)
#define VIDEO_MEMORY_WAIT_TIMEOUT_NS (100u * 1000u * 1000u)

typedef enum VideoMemoryDomain {
    VIDEO_MEMORY_SHARED = 0,
    VIDEO_MEMORY_PRIVATE = 1,
    VIDEO_MEMORY_STAGING = 2,
} VideoMemoryDomain;

typedef struct VideoSlabInfo {
    vpointer gpuBase;
    pointer cpuBase;
    bytes length;
    any driverHandle;
    VideoMemoryDomain domain;
    bool umaZeroCopy;
} VideoSlabInfo;

/**
 * Thin driver seam implemented per graphics API (metal/, vulkan/, direct/, wgpu/).
 * Public consumers never touch driver handles; drivers register a VideoPoolDriver.
 */
typedef struct VideoPoolDriver {
    string name;
    any context;
    bool uma;
    bool (*allocSlab)(any context, VideoMemoryDomain domain, bytes length, VideoSlabInfo *outSlab);
    fn (*freeSlab)(any context, const VideoSlabInfo *slab);
    bool (*copyHostToDevice)(any context, const VideoSlabInfo *slab, bytes offset, const_any src, bytes length);
    bool (*copyDeviceToHost)(any context, any dst, const VideoSlabInfo *slab, bytes offset, bytes length);
    fn (*destroyContext)(any context);
} VideoPoolDriver;

typedef struct VideoPool VideoPool;
typedef struct VideoArena VideoArena;

/** Pool lifecycle (owns growable GPU slabs; slabs grow indefinitely until OOM). */
VideoPool *VideoPool_1(bytes slabBytes);
VideoPool *VideoPool_2(const VideoPoolDriver *driver, bytes slabBytes);
#define VideoPool(...) CONSTRUCTOR_DISPATCH(VideoPool, __VA_ARGS__)
VideoPool *VideoPool_software(bytes slabBytes);
fn VideoPool_destroy(VideoPool *pool);

bool VideoPool_isUma(const VideoPool *pool);
string VideoPool_driverName(const VideoPool *pool);
bytes VideoPool_slabBytes(const VideoPool *pool);
bytes VideoPool_slabCount(const VideoPool *pool);
bytes VideoPool_totalBytes(const VideoPool *pool);
bytes VideoPool_usedBytes(const VideoPool *pool);
u64 VideoPool_exhaustionCount(const VideoPool *pool);

/**
 * Virtual GPU Arena lifecycle (per-subsystem arena over a VideoPool).
 * maxBytes == 0 means unbounded growth; maxBytes > 0 enforces a hard budget.
 */
VideoArena *VideoArena_3(VideoPool *pool, VideoMemoryDomain domain, bytes maxBytes);
#define VideoArena(...) CONSTRUCTOR_DISPATCH(VideoArena, __VA_ARGS__)
fn VideoArena_destroy(VideoArena *arena);

vpointer VideoArena_alloc(VideoArena *arena, bytes numBytes, bytes alignment);
fn VideoArena_free(VideoArena *arena, vpointer addr);
fn VideoArena_reset(VideoArena *arena);
fn VideoArena_trim(VideoArena *arena);

VideoPool *VideoArena_pool(const VideoArena *arena);
VideoMemoryDomain VideoArena_domain(const VideoArena *arena);
bytes VideoArena_usedBytes(const VideoArena *arena);
bytes VideoArena_capacity(const VideoArena *arena);
bytes VideoArena_maxBytes(const VideoArena *arena);
u64 VideoArena_exhaustionCount(const VideoArena *arena);

/**
 * CPU < to  GPU bridge (vpointer < to  pointer).
 * On UMA / VIDEO_MEMORY_SHARED, VideoMemory_toPointer returns the direct CPU
 * pointer in O(1) (0 if private/non-host-visible or unknown).
 */
pointer VideoMemory_toPointer(const VideoPool *pool, vpointer addr);
any VideoMemory_map(const VideoPool *pool, vpointer addr);
vpointer VideoMemory_fromPointer(const VideoPool *pool, pointer cpuAddr);

/** Domain-aware transfers: direct memcpy on UMA/shared, driver staging blit on private. */
bool VideoMemory_write(const VideoPool *pool, vpointer dst, const_any src, bytes length);
bool VideoMemory_read(const VideoPool *pool, any dst, vpointer src, bytes length);
bool VideoMemory_contains(const VideoPool *pool, vpointer addr, bytes length);
bool VideoMemory_isUma(const VideoPool *pool);

#if defined(VEX_PLATFORM_APPLE)
VideoPool *VideoPool_metal(bytes slabBytes);
#endif

#endif
