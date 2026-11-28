#include "vex.h"
#include "vio/vio.h"
#include "nio/graphvex_memory.h"

#if !defined(VEX_PLATFORM_APPLE)
#error "graphvex/metal/mtl_vpool.m requires VEX_PLATFORM_APPLE"
#endif

#include "metal/mtl_internal.h"
#import <Metal/MTLBlitCommandEncoder.h>
#import <dispatch/dispatch.h>
#include <string.h>
#include <sys/mman.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: MetalImagePool
 * ============================================================================
 * Implements the ImagePoolDriver seam on Apple Silicon and macOS Metal.
 * IMAGE_MEMORY_SHARED slabs are backed by 16 KiB page-aligned PROT_NONE-guarded
 * mmap regions wrapped zero-copy via newBufferWithBytesNoCopy:, exposing real
 * 64-bit MTLBuffer.gpuAddress as vpointer alongside a direct CPU pointer.
 * IMAGE_MEMORY_PRIVATE slabs allocate StorageModePrivate buffers and stage
 * reads/writes via bounded blit command buffers (100 ms timeout).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: MetalImagePool (metal/mtl_vpool.m)
 * ============================================================================
 * Metal slab pool driver for ImagePool.
 *
 * --CONSTRUCTORS-------------------------
 * public   ImagePool_metal(...)   creates Metal-backed ImagePool
 *
 * --FUNCTIONS----------------------------
 * private  mtlAllocSlab           allocates guard-paged shared or private slab
 * private  mtlFreeSlab            releases MTLBuffer and unmaps guard-paged slab
 * private  mtlCopyHostToDevice    bounded blit upload into private VRAM
 * private  mtlCopyDeviceToHost    bounded blit readback from private VRAM
 * private  mtlDestroyContext      releases Metal queue and device handles
 * ============================================================================
 */

static bytes roundToPage(bytes n)
{
    bytes page = (bytes) MEMORY_PAGE_SIZE;
    bytes mask = page - 1u;
    if (n > SIZE_MAX - mask)
        return 0u;

    return (n + mask) & ~mask;
}

;;SETTER
static fn mtlSlabInfoSet(ImageSlabInfo *s, vpointer gpuBase, pointer cpuBase,
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

static bool mtlWaitCommandBuffer(id<MTLCommandBuffer> cmd)
{
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    dispatch_retain(sem);
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        (discard) completed;
        dispatch_semaphore_signal(sem);
        dispatch_release(sem);
    }];
    [cmd commit];
    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, (i64) IMAGE_MEMORY_WAIT_TIMEOUT_NS);
    i64 waited = (i64) dispatch_semaphore_wait(sem, deadline);
    dispatch_release(sem);
    if (waited != 0)
        return false;

    return cmd.status == MTLCommandBufferStatusCompleted;
}

static bool mtlAllocSlab(any context, ImageMemoryDomain domain, bytes length, ImageSlabInfo *outSlab)
{
    MtlPoolContext *ctx = (MtlPoolContext*) context;
    if (ctx == nullptr || outSlab == nullptr || length == 0u)
        return false;

    bytes alignedLen = roundToPage(length);
    if (alignedLen == 0u)
        return false;

    id<MTLDevice> dev = MtlPoolContext_device(ctx);
    SecureMemoryArena *sa = Graphvex_secureArena();
    SecureHandle sh = SecureMemoryArena_alloc(sa, 0u, sizeof(MtlSlabHandle));
    if (!SecureHandle_isValid(sh))
        return false;

    MtlSlabHandle *handle = (MtlSlabHandle*) SecureMemoryArena_resolve(sa, sh);
    if (handle == nullptr) {
        (discard) SecureMemoryArena_free(sa, sh);
        return false;
    }
    MtlSlabHandle_setSelfHandle(handle, sh);
    if (domain == IMAGE_MEMORY_SHARED || domain == IMAGE_MEMORY_STAGING) {
        bytes page = (bytes) MEMORY_PAGE_SIZE;
        if (alignedLen > SIZE_MAX - (2u * page)) {
            (discard) SecureMemoryArena_free(sa, sh);
            return false;
        }
        bytes totalLen = alignedLen + (2u * page);
        any mapping = mmap(nullptr, totalLen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (mapping == MAP_FAILED) {
            (discard) SecureMemoryArena_free(sa, sh);
            return false;
        }
        u8 *mapBase = (u8*) mapping;
        if (mprotect(mapBase, page, PROT_NONE) != 0 ||
            mprotect(mapBase + page + alignedLen, page, PROT_NONE) != 0) {
            munmap(mapBase, totalLen);
            (discard) SecureMemoryArena_free(sa, sh);
            return false;
        }
        any raw = (any) (mapBase + page);
        id<MTLBuffer> buf = [dev newBufferWithBytesNoCopy:raw
                                                   length:alignedLen
                                                  options:MTLResourceStorageModeShared
                                              deallocator:nil];
        if (buf == nil) {
            munmap(mapBase, totalLen);
            (discard) SecureMemoryArena_free(sa, sh);
            return false;
        }
        MtlSlabHandle_setBuffer(handle, buf);
        MtlSlabHandle_setMmapBase(handle, mapBase);
        MtlSlabHandle_setMmapLen(handle, totalLen);
        mtlSlabInfoSet(outSlab, (vpointer) buf.gpuAddress, (pointer) raw,
                       alignedLen, handle, domain, MtlPoolContext_uma(ctx));
        return true;
    }
    id<MTLBuffer> priv = [dev newBufferWithLength:alignedLen options:MTLResourceStorageModePrivate];
    if (priv == nil) {
        (discard) SecureMemoryArena_free(sa, sh);
        return false;
    }
    MtlSlabHandle_setBuffer(handle, priv);
    mtlSlabInfoSet(outSlab, (vpointer) priv.gpuAddress, (pointer) 0,
                   alignedLen, handle, domain, false);
    return true;
}

static fn mtlFreeSlab(any context, const ImageSlabInfo *slab)
{
    (discard) context;
    if (slab == nullptr || MtlImageSlab_driverHandle(slab) == nullptr)
        return;

    MtlSlabHandle *handle = (MtlSlabHandle*) MtlImageSlab_driverHandle(slab);
    SecureHandle sh = MtlSlabHandle_selfHandle(handle);
    id<MTLBuffer> buf = MtlSlabHandle_buffer(handle);
    if (buf != nil)
        [buf release];

    if (MtlSlabHandle_mmapBase(handle) != nullptr)
        munmap(MtlSlabHandle_mmapBase(handle), MtlSlabHandle_mmapLen(handle));

    if (SecureHandle_isValid(sh))
        (discard) SecureMemoryArena_free(Graphvex_secureArena(), sh);
}

static bool mtlCopyHostToDevice(any context, const ImageSlabInfo *slab, bytes offset, const_any src, bytes length)
{
    MtlPoolContext *ctx = (MtlPoolContext*) context;
    if (ctx == nullptr || slab == nullptr || MtlImageSlab_driverHandle(slab) == nullptr || src == nullptr || length == 0u)
        return false;

    @autoreleasepool {
        MtlSlabHandle *handle = (MtlSlabHandle*) MtlImageSlab_driverHandle(slab);
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

static bool mtlCopyDeviceToHost(any context, any dst, const ImageSlabInfo *slab, bytes offset, bytes length)
{
    MtlPoolContext *ctx = (MtlPoolContext*) context;
    if (ctx == nullptr || slab == nullptr || MtlImageSlab_driverHandle(slab) == nullptr || dst == nullptr || length == 0u)
        return false;

    @autoreleasepool {
        MtlSlabHandle *handle = (MtlSlabHandle*) MtlImageSlab_driverHandle(slab);
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

static fn mtlDestroyContext(any context)
{
    MtlPoolContext *ctx = (MtlPoolContext*) context;
    if (ctx == nullptr)
        return;

    SecureHandle sh = MtlPoolContext_selfHandle(ctx);
    if (MtlPoolContext_queue(ctx) != nil)
        [MtlPoolContext_queue(ctx) release];

    if (MtlPoolContext_device(ctx) != nil)
        [MtlPoolContext_device(ctx) release];

    if (SecureHandle_isValid(sh))
        (discard) SecureMemoryArena_free(Graphvex_secureArena(), sh);
}

;;CONSTRUCTOR
/**
 * Creates a Metal-backed ImagePool on Apple targets.
 * @param slabBytes slab size in bytes (0 selects IMAGE_MEMORY_DEFAULT_SLAB_BYTES)
 * @return newly allocated ImagePool, or nullptr when Metal is unavailable
 */
ImagePool *ImagePool_metal(bytes slabBytes)
{
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (dev == nil) {
        THROW("MetalImagePool: no Metal device available");
        return nullptr;
    }
    id<MTLCommandQueue> queue = [dev newCommandQueue];
    if (queue == nil) {
        [dev release];
        THROW("MetalImagePool: failed to create command queue");
        return nullptr;
    }
    SecureMemoryArena *sa = Graphvex_secureArena();
    SecureHandle sh = SecureMemoryArena_alloc(sa, 0u, sizeof(MtlPoolContext));
    MtlPoolContext *ctx = SecureHandle_isValid(sh)
        ? (MtlPoolContext*) SecureMemoryArena_resolve(sa, sh)
        : nullptr;
    if (ctx == nullptr) {
        if (SecureHandle_isValid(sh))
            (discard) SecureMemoryArena_free(sa, sh);

        [queue release];
        [dev release];
        THROW("MetalImagePool: failed to allocate context");
        return nullptr;
    }
    MtlPoolContext_setSelfHandle(ctx, sh);
    MtlPoolContext_setDevice(ctx, dev);
    MtlPoolContext_setQueue(ctx, queue);
    MtlPoolContext_setUma(ctx, dev.hasUnifiedMemory ? true : false);
    ImagePoolDriver driver;
    memset(&driver, 0, sizeof(ImagePoolDriver));
    driver.name = "metal";
    driver.context = ctx;
    driver.uma = MtlPoolContext_uma(ctx);
    driver.allocSlab = mtlAllocSlab;
    driver.freeSlab = mtlFreeSlab;
    driver.copyHostToDevice = mtlCopyHostToDevice;
    driver.copyDeviceToHost = mtlCopyDeviceToHost;
    driver.destroyContext = mtlDestroyContext;
    ImagePool *pool = ImagePool(&driver, slabBytes);
    if (pool == nullptr)
        mtlDestroyContext(ctx);

    return pool;
}
