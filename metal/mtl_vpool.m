#include "vex.h"
#include "vio/vio.h"
#include "nio/graphvex_memory.h"

#if !defined(VEX_PLATFORM_APPLE)
#error "graphvex/metal/mtl_vpool.m requires VEX_PLATFORM_APPLE"
#endif

#include "metal/mtl_internal.h"
#import <Metal/MTLBlitCommandEncoder.h>
#import <dispatch/dispatch.h>
#include <sys/mman.h>

;;DEFINITION
/** MetalVideoPool implements the VideoPoolDriver seam on Apple Silicon and
 * macOS Metal. VIDEO_MEMORY_SHARED slabs are backed by 16 KiB page-aligned
 * mmap regions wrapped zero-copy via newBufferWithBytesNoCopy:, exposing real
 * 64-bit MTLBuffer.gpuAddress as vpointer alongside a direct CPU pointer.
 * VIDEO_MEMORY_PRIVATE slabs allocate StorageModePrivate buffers and stage
 * reads/writes via bounded blit command buffers (100 ms timeout). */

;;OVERVIEW
/** CLASS: MetalVideoPool (metal/mtl_vpool.m)
 * STRUCT FIELDS: MtlPoolContext (device, commandQueue, uma), MtlSlabHandle
 * (buffer, mmapBase, mmapLen) in metal/mtl_internal.h.
 * FUNCTION REGISTRY: VideoPool_metal.
 * CONTRACT: vendor Metal types stay strictly inside this .m file; every GPU
 * blit wait obeys VIDEO_MEMORY_WAIT_TIMEOUT_NS (100 ms max). */

static bytes roundToPage(bytes n) {
    bytes page = (bytes) MEMORY_PAGE_SIZE;
    bytes mask = page - 1u;
    if (n > SIZE_MAX - mask)
        return 0u;
    return (n + mask) & ~mask;
}

static bool mtlWaitCommandBuffer(id<MTLCommandBuffer> cmd) {
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    dispatch_retain(sem);
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        (discard) completed;
        dispatch_semaphore_signal(sem);
        dispatch_release(sem);
    }];
    [cmd commit];
    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, (i64) VIDEO_MEMORY_WAIT_TIMEOUT_NS);
    i64 waited = (i64) dispatch_semaphore_wait(sem, deadline);
    dispatch_release(sem);
    if (waited != 0)
        return false;
    return cmd.status == MTLCommandBufferStatusCompleted;
}

static bool mtlAllocSlab(any context, VideoMemoryDomain domain, bytes length, VideoSlabInfo *outSlab) {
    MtlPoolContext *ctx = (MtlPoolContext*) context;
    if (ctx == nullptr || outSlab == nullptr || length == 0u)
        return false;
    bytes alignedLen = roundToPage(length);
    if (alignedLen == 0u)
        return false;
    id<MTLDevice> dev = MtlPoolContext_device(ctx);
    MtlSlabHandle *handle = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *handle);
    if (handle == nullptr)
        return false;
    memset(handle, 0, sizeof *handle);
    if (domain == VIDEO_MEMORY_SHARED || domain == VIDEO_MEMORY_STAGING) {
        any raw = mmap(nullptr, alignedLen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (raw == MAP_FAILED) {
            MemoryArena_free(Graphvex_arena(), handle);
            return false;
        }
        id<MTLBuffer> buf = [dev newBufferWithBytesNoCopy:raw
                                                   length:alignedLen
                                                  options:MTLResourceStorageModeShared
                                              deallocator:nil];
        if (buf == nil) {
            munmap(raw, alignedLen);
            MemoryArena_free(Graphvex_arena(), handle);
            return false;
        }
        MtlSlabHandle_setBuffer(handle, buf);
        MtlSlabHandle_setMmapBase(handle, raw);
        MtlSlabHandle_setMmapLen(handle, alignedLen);
        *outSlab = (VideoSlabInfo) {
            .gpuBase = (vpointer) buf.gpuAddress,
            .cpuBase = (pointer) raw,
            .length = alignedLen,
            .driverHandle = handle,
            .domain = domain,
            .umaZeroCopy = MtlPoolContext_uma(ctx),
        };
        return true;
    }
    id<MTLBuffer> priv = [dev newBufferWithLength:alignedLen options:MTLResourceStorageModePrivate];
    if (priv == nil) {
        MemoryArena_free(Graphvex_arena(), handle);
        return false;
    }
    MtlSlabHandle_setBuffer(handle, priv);
    *outSlab = (VideoSlabInfo) {
        .gpuBase = (vpointer) priv.gpuAddress,
        .cpuBase = (pointer) 0,
        .length = alignedLen,
        .driverHandle = handle,
        .domain = domain,
        .umaZeroCopy = false,
    };
    return true;
}

static fn mtlFreeSlab(any context, const VideoSlabInfo *slab) {
    (discard) context;
    if (slab == nullptr || MtlVideoSlab_driverHandle(slab) == nullptr)
        return;
    MtlSlabHandle *handle = (MtlSlabHandle*) MtlVideoSlab_driverHandle(slab);
    id<MTLBuffer> buf = MtlSlabHandle_buffer(handle);
    if (buf != nil)
        [buf release];
    if (MtlSlabHandle_mmapBase(handle) != nullptr)
        munmap(MtlSlabHandle_mmapBase(handle), MtlSlabHandle_mmapLen(handle));
    MemoryArena_free(Graphvex_arena(), handle);
}

static bool mtlCopyHostToDevice(any context, const VideoSlabInfo *slab, bytes offset, const_any src, bytes length) {
    MtlPoolContext *ctx = (MtlPoolContext*) context;
    if (ctx == nullptr || slab == nullptr || MtlVideoSlab_driverHandle(slab) == nullptr || src == nullptr || length == 0u)
        return false;
    @autoreleasepool {
        MtlSlabHandle *handle = (MtlSlabHandle*) MtlVideoSlab_driverHandle(slab);
        id<MTLDevice> dev = MtlPoolContext_device(ctx);
        id<MTLBuffer> staging = [dev newBufferWithBytes:src length:length options:MTLResourceStorageModeShared];
        if (staging == nil)
            return false;
        id<MTLCommandBuffer> cmd = [MtlPoolContext_queue(ctx) commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit copyFromBuffer:staging sourceOffset:0 toBuffer:MtlSlabHandle_buffer(handle) destinationOffset:offset size:length];
        [blit endEncoding];
        bool ok = mtlWaitCommandBuffer(cmd);
        [staging release];
        return ok;
    }
}

static bool mtlCopyDeviceToHost(any context, any dst, const VideoSlabInfo *slab, bytes offset, bytes length) {
    MtlPoolContext *ctx = (MtlPoolContext*) context;
    if (ctx == nullptr || slab == nullptr || MtlVideoSlab_driverHandle(slab) == nullptr || dst == nullptr || length == 0u)
        return false;
    @autoreleasepool {
        MtlSlabHandle *handle = (MtlSlabHandle*) MtlVideoSlab_driverHandle(slab);
        id<MTLDevice> dev = MtlPoolContext_device(ctx);
        id<MTLBuffer> staging = [dev newBufferWithLength:length options:MTLResourceStorageModeShared];
        if (staging == nil)
            return false;
        id<MTLCommandBuffer> cmd = [MtlPoolContext_queue(ctx) commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit copyFromBuffer:MtlSlabHandle_buffer(handle) sourceOffset:offset toBuffer:staging destinationOffset:0 size:length];
        [blit endEncoding];
        bool ok = mtlWaitCommandBuffer(cmd);
        if (ok)
            memcpy(dst, staging.contents, length);
        [staging release];
        return ok;
    }
}

static fn mtlDestroyContext(any context) {
    MtlPoolContext *ctx = (MtlPoolContext*) context;
    if (ctx == nullptr)
        return;
    if (MtlPoolContext_queue(ctx) != nil)
        [MtlPoolContext_queue(ctx) release];
    if (MtlPoolContext_device(ctx) != nil)
        [MtlPoolContext_device(ctx) release];
    MemoryArena_free(Graphvex_arena(), ctx);
}

;;CONSTRUCTOR
/**
 * Creates a Metal-backed VideoPool on Apple targets.
 * @param slabBytes slab size in bytes (0 selects VIDEO_MEMORY_DEFAULT_SLAB_BYTES)
 * @return newly allocated VideoPool, or nullptr when Metal is unavailable
 */
VideoPool *VideoPool_metal(bytes slabBytes)
{
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (dev == nil) {
        THROW("MetalVideoPool: no Metal device available");
        return nullptr;
    }
    id<MTLCommandQueue> queue = [dev newCommandQueue];
    if (queue == nil) {
        [dev release];
        THROW("MetalVideoPool: failed to create command queue");
        return nullptr;
    }
    MtlPoolContext *ctx = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *ctx);
    if (ctx == nullptr) {
        [queue release];
        [dev release];
        THROW("MetalVideoPool: failed to allocate context");
        return nullptr;
    }
    memset(ctx, 0, sizeof *ctx);
    MtlPoolContext_setDevice(ctx, dev);
    MtlPoolContext_setQueue(ctx, queue);
    MtlPoolContext_setUma(ctx, dev.hasUnifiedMemory ? true : false);
    VideoPoolDriver driver = {
        .name = "metal",
        .context = ctx,
        .uma = MtlPoolContext_uma(ctx),
        .allocSlab = mtlAllocSlab,
        .freeSlab = mtlFreeSlab,
        .copyHostToDevice = mtlCopyHostToDevice,
        .copyDeviceToHost = mtlCopyDeviceToHost,
        .destroyContext = mtlDestroyContext,
    };
    VideoPool *pool = VideoPool(&driver, slabBytes);
    if (pool == nullptr)
        mtlDestroyContext(ctx);
    return pool;
}
