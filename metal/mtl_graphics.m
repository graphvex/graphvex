#include "vex.h"
#include "metal/metal.h"
#include "metal/mtl_internal.h"
#include "shader/shader.h"
#include "nio/graphvex_memory.h"

#import <Foundation/NSString.h>
#import <Foundation/NSError.h>
#import <Metal/MTLTexture.h>
#import <Metal/MTLLibrary.h>
#import <Metal/MTLComputePipeline.h>
#import <Metal/MTLComputeCommandEncoder.h>
#import <Metal/MTLRenderPipeline.h>
#import <Metal/MTLRenderPass.h>
#import <Metal/MTLRenderCommandEncoder.h>
#import <Metal/MTLBlitCommandEncoder.h>
#import <QuartzCore/CALayer.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <AppKit/NSView.h>
#import <AppKit/NSWindow.h>
#import <dispatch/dispatch.h>
#include <math.h>

#define MTL_ERR_DEVICE_REMOVED 11

;;DEFINITION
/** MetalGraphicsDriver implements the core/driver.h GraphicsDriver seam on
 * Apple Silicon and macOS Metal over vio/ VideoPool slabs. Clear, cropped/
 * strided copy, custom compute dispatch, hardware-instanced quad draw, and
 * swapchain-free CAMetalLayer presentation execute directly against
 * MTLBuffer-backed vpointer slices with 100 ms bounded waits and latched
 * device-lost handling. */

;;OVERVIEW
/** CLASS: MetalGraphicsDriver (metal/mtl_graphics.m)
 * STRUCT FIELDS: MtlGraphicsContext (device, queue, ownsDevice, deviceLost,
 * builtinLibrary, clearPipeline, copyPipeline, presentPipeline,
 * customPipelines, renderPipelines, renderTex, renderTexW, renderTexH,
 * customCount).
 * FUNCTION REGISTRY: Graphics_metal.
 * CONTRACT: vendor Metal and QuartzCore types stay strictly inside this .m
 * file; native BGRA drawable conversion happens only inside presentImage. */



;;GETTER
static inline id<MTLDevice> MtlGfx_device(const MtlGraphicsContext *c) { return (*c).device; }
;;SETTER
static inline fn MtlGfx_setDevice(MtlGraphicsContext *c, id<MTLDevice> d)
{
    (*c).device = d;
}
;;GETTER
static inline id<MTLCommandQueue> MtlGfx_queue(const MtlGraphicsContext *c) { return (*c).queue; }
;;SETTER
static inline fn MtlGfx_setQueue(MtlGraphicsContext *c, id<MTLCommandQueue> q)
{
    (*c).queue = q;
}
;;GETTER
static inline id<MTLLibrary> MtlGfx_library(const MtlGraphicsContext *c) { return (*c).builtinLibrary; }
;;SETTER
static inline fn MtlGfx_setLibrary(MtlGraphicsContext *c, id<MTLLibrary> l)
{
    (*c).builtinLibrary = l;
}
;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_clearPipe(const MtlGraphicsContext *c) { return (*c).clearPipeline; }
;;SETTER
static inline fn MtlGfx_setClearPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).clearPipeline = p;
}
;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_copyPipe(const MtlGraphicsContext *c) { return (*c).copyPipeline; }
;;SETTER
static inline fn MtlGfx_setCopyPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).copyPipeline = p;
}
;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_copyToTexPipe(const MtlGraphicsContext *c) { return (*c).copyToTexPipeline; }
;;SETTER
static inline fn MtlGfx_setCopyToTexPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).copyToTexPipeline = p;
}
;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_presentPipe(const MtlGraphicsContext *c) { return (*c).presentPipeline; }
;;SETTER
static inline fn MtlGfx_setPresentPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).presentPipeline = p;
}
;;GETTER
static inline u32 MtlGfx_customCount(const MtlGraphicsContext *c)
{
    return (*c).customCount;
}
;;GETTER
static inline bool MtlGfx_surfacePurgeable(const MtlGraphicsContext *c)
{
    if (!c)
        return false;

    return (*c).surfacePurgeable;
}
;;SETTER
static inline fn MtlGfx_setSurfacePurgeable(MtlGraphicsContext *c, bool v)
{
    if (!c)
        return;

    (*c).surfacePurgeable = v;
}
;;SETTER
static inline fn MtlGfx_setCustomCount(MtlGraphicsContext *c, u32 n)
{
    (*c).customCount = n;
}
;;GETTER
static inline u32 MtlGfx_cachedLibCount(const MtlGraphicsContext *c)
{
    return (*c).cachedLibCount;
}
;;SETTER
static inline fn MtlGfx_setCachedLibCount(MtlGraphicsContext *c, u32 n)
{
    (*c).cachedLibCount = n;
}
;;GETTER
static inline id<MTLLibrary> MtlGfx_cachedLibAt(const MtlGraphicsContext *c, u32 idx)
{
    return (*c).cachedLibraries[idx];
}
;;SETTER
static inline fn MtlGfx_setCachedLibAt(MtlGraphicsContext *c, u32 idx, id<MTLLibrary> lib)
{
    (*c).cachedLibraries[idx] = lib;
}
;;GETTER
static inline string MtlGfx_cachedLibPathAt(const MtlGraphicsContext *c, u32 idx)
{
    return (*c).cachedLibPaths[idx];
}
;;SETTER
static inline fn MtlGfx_setCachedLibPathAt(MtlGraphicsContext *c, u32 idx, string path)
{
    if (!c || !path || idx >= MTL_MAX_CACHED_LIBS)
        return;

    u32 i = 0u;
    while (i + 1u < MTL_LIB_PATH_CAP && path[i] != '\0') {
        (*c).cachedLibPaths[idx][i] = path[i];
        i++;
    }
    (*c).cachedLibPaths[idx][i] = '\0';
}
;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_customPipeAt(const MtlGraphicsContext *c, u32 idx) { return (*c).customPipelines[idx]; }
;;SETTER
static inline fn MtlGfx_setCustomPipeAt(MtlGraphicsContext *c, u32 idx, id<MTLComputePipelineState> p)
{
    (*c).customPipelines[idx] = p;
}
;;GETTER
static inline id<MTLRenderPipelineState> MtlGfx_renderPipeAt(const MtlGraphicsContext *c, u32 idx) { return (*c).renderPipelines[idx]; }
;;SETTER
static inline fn MtlGfx_setRenderPipeAt(MtlGraphicsContext *c, u32 idx, id<MTLRenderPipelineState> p)
{
    (*c).renderPipelines[idx] = p;
}
;;GETTER
static inline id<MTLRenderPipelineState> MtlGfx_surfaceRenderPipeAt(const MtlGraphicsContext *c, u32 idx) { return (*c).surfaceRenderPipelines[idx]; }
;;SETTER
static inline fn MtlGfx_setSurfaceRenderPipeAt(MtlGraphicsContext *c, u32 idx, id<MTLRenderPipelineState> p)
{
    (*c).surfaceRenderPipelines[idx] = p;
}
;;GETTER
static inline id<MTLTexture> MtlGfx_renderTex(const MtlGraphicsContext *c) { return (*c).renderTex; }
;;SETTER
static inline fn MtlGfx_setRenderTex(MtlGraphicsContext *c, id<MTLTexture> t, u32 w, u32 h) {
    (*c).renderTex = t;
    (*c).renderTexW = w;
    (*c).renderTexH = h;
}
;;GETTER
static inline u32 MtlGfx_renderTexW(const MtlGraphicsContext *c)
{
    return (*c).renderTexW;
}
;;GETTER
static inline u32 MtlGfx_renderTexH(const MtlGraphicsContext *c)
{
    return (*c).renderTexH;
}
;;GETTER
static inline IOSurfaceRef MtlGfx_swapSurfaceAt(const MtlGraphicsContext *c, u32 idx)
{
    return (*c).swapSurfaces[idx];
}
;;SETTER
static inline fn MtlGfx_setSwapSurfaceAt(MtlGraphicsContext *c, u32 idx, IOSurfaceRef s)
{
    (*c).swapSurfaces[idx] = s;
}
;;GETTER
static inline id<MTLTexture> MtlGfx_swapTexAt(const MtlGraphicsContext *c, u32 idx)
{
    return (*c).swapTextures[idx];
}
;;SETTER
static inline fn MtlGfx_setSwapTexAt(MtlGraphicsContext *c, u32 idx, id<MTLTexture> t)
{
    (*c).swapTextures[idx] = t;
}
;;GETTER
static inline id<MTLTexture> MtlGfx_swapSrgbAt(const MtlGraphicsContext *c, u32 idx)
{
    return (*c).swapSrgbViews[idx];
}
;;SETTER
static inline fn MtlGfx_setSwapSrgbAt(MtlGraphicsContext *c, u32 idx, id<MTLTexture> t)
{
    (*c).swapSrgbViews[idx] = t;
}
;;GETTER
static inline u32 MtlGfx_swapW(const MtlGraphicsContext *c)
{
    return (*c).swapW;
}
;;GETTER
static inline u32 MtlGfx_swapH(const MtlGraphicsContext *c)
{
    return (*c).swapH;
}
;;SETTER
static inline fn MtlGfx_setSwapSize(MtlGraphicsContext *c, u32 w, u32 h)
{
    (*c).swapW = w;
    (*c).swapH = h;
}
;;GETTER
static inline u32 MtlGfx_swapIndex(const MtlGraphicsContext *c)
{
    return (*c).swapIndex;
}
;;SETTER
static inline fn MtlGfx_setSwapIndex(MtlGraphicsContext *c, u32 idx)
{
    (*c).swapIndex = idx;
}
;;GETTER
static inline bool MtlGfx_ownsDevice(const MtlGraphicsContext *c)
{
    return (*c).ownsDevice;
}
;;SETTER
static inline fn MtlGfx_setOwnsDevice(MtlGraphicsContext *c, bool v)
{
    (*c).ownsDevice = v;
}
;;GETTER
static inline id<MTLCommandBuffer> MtlGfx_pendingPresent(const MtlGraphicsContext *c) { return (*c).pendingPresent; }
;;SETTER
static inline fn MtlGfx_setPendingPresent(MtlGraphicsContext *c, id<MTLCommandBuffer> cmd)
{
    (*c).pendingPresent = cmd;
}
;;GETTER
static inline bool *MtlGfx_deviceLostPtr(MtlGraphicsContext *c)
{
    return &(*c).deviceLost;
}
;;GETTER
static inline bool MtlGfx_syncPresent(const MtlGraphicsContext *c)
{
    return (*c).syncPresent;
}
;;SETTER
static inline fn MtlGfx_setSyncPresent(MtlGraphicsContext *c, bool v)
{
    (*c).syncPresent = v;
}
;;GETTER
static inline SecureHandle MtlGfx_selfHandle(const MtlGraphicsContext *c)
{
    return (*c).selfHandle;
}
;;SETTER
static inline fn MtlGfx_setSelfHandle(MtlGraphicsContext *c, SecureHandle h)
{
    (*c).selfHandle = h;
}

static const u8 MTL_BUILTIN_EMBED[] = {
#embed "builtin.metal"
    , 0
};

/**
 * Compiles the embedded Metal driver builtin library (clear, copy, present
 * kernels) once at context init.
 * @param dev Metal device
 * @return compiled MTLLibrary or nil on compile failure
 */
static id<MTLLibrary> mtlLoadBuiltinLibrary(id<MTLDevice> dev) {
    NSError *err = nil;
    NSString *src = [NSString stringWithUTF8String:(string) MTL_BUILTIN_EMBED];
    return [dev newLibraryWithSource:src options:nil error:&err];
}

static bool mtlResolveBuffer(const VideoPool *pool, vpointer addr, bytes len,
                             id<MTLBuffer> *outBuf, bytes *outOffset) {
    if (pool == nullptr || addr == 0u || len == 0u || outBuf == nullptr || outOffset == nullptr)
        return false;
    VideoSlabInfo slab = { 0 };
    bytes off = 0u;
    if (!VideoPool_findByGpu(pool, addr, len, &slab, &off))
        return false;
    any rawHandle = MtlVideoSlab_driverHandle(&slab);
    if (rawHandle == nullptr)
        return false;
    MtlSlabHandle *handle = (MtlSlabHandle*) rawHandle;
    id<MTLBuffer> buf = MtlSlabHandle_buffer(handle);
    if (buf == nil)
        return false;
    *outBuf = buf;
    *outOffset = off;
    return true;
}

static bool mtlWaitBounded(MtlGraphicsContext *ctx, id<MTLCommandBuffer> cmd) {
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    dispatch_retain(sem);
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        (discard) completed;
        dispatch_semaphore_signal(sem);
        dispatch_release(sem);
    }];
    [cmd commit];
    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, (i64) FENCE_WAIT_TIMEOUT_NS);
    i64 waited = (i64) dispatch_semaphore_wait(sem, deadline);
    dispatch_release(sem);
    if (waited != 0)
        return false;
    if (cmd.status != MTLCommandBufferStatusCompleted) {
        if (cmd.error != nil && cmd.error.code == MTL_ERR_DEVICE_REMOVED) {
            bool *lost = MtlGfx_deviceLostPtr(ctx);
            *lost = true;
        }
        return false;
    }
    return true;
}

static fn mtlDispatch2D(id<MTLComputeCommandEncoder> enc,
                        id<MTLComputePipelineState> pipe,
                        u32 width, u32 height) {
    NSUInteger tw = pipe.threadExecutionWidth;
    if (tw == 0u)
        tw = 16u;
    NSUInteger th = pipe.maxTotalThreadsPerThreadgroup / tw;
    if (th == 0u)
        th = 1u;
    if (th > 16u)
        th = 16u;
    MTLSize grid = MTLSizeMake((NSUInteger) width, (NSUInteger) height, 1u);
    MTLSize group = MTLSizeMake(tw, th, 1u);
    [enc dispatchThreads:grid threadsPerThreadgroup:group];
}

static id<MTLTexture> mtlEnsureRenderTexture(MtlGraphicsContext *ctx, u32 width, u32 height) {
    if (width == 0u || height == 0u)
        return nil;

    id<MTLTexture> tex = MtlGfx_renderTex(ctx);
    u32 curW = MtlGfx_renderTexW(ctx);
    u32 curH = MtlGfx_renderTexH(ctx);
    if (tex != nil && curW >= width && curH >= height) {
        MTLPurgeableState prev = [tex setPurgeableState:MTLPurgeableStateNonVolatile];
        if (prev != MTLPurgeableStateEmpty)
            return tex;
    }

    u32 allocW = width > curW ? width : curW;
    u32 allocH = height > curH ? height : curH;
    if (tex != nil)
        [tex release];

    MTLTextureDescriptor *desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                     width:(NSUInteger) allocW
                                    height:(NSUInteger) allocH
                                 mipmapped:NO];
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> created = [MtlGfx_device(ctx) newTextureWithDescriptor:desc];
    if (created == nil) {
        MtlGfx_setRenderTex(ctx, nil, 0u, 0u);
        return nil;
    }

    MtlGfx_setRenderTex(ctx, created, allocW, allocH);
    return created;
}

static bool mtlEncodeClear(MtlGraphicsContext *ctx, const VideoPool *pool,
                           id<MTLCommandBuffer> cmdBuf, GraphicsCommand cmd) {
    GraphicsImage dst = cmd.target;
    if (!GraphicsImage_isValid(&dst))
        return false;
    if (PixelFormat_bytesPerPixel(dst.format) != 4u)
        return false;
    GraphicsRect bounds = GraphicsRect_4(0, 0, (i32) dst.width, (i32) dst.height);
    GraphicsRect clip = GraphicsRect_isEmpty(cmd.scissor)
        ? bounds
        : GraphicsRect_intersect(bounds, cmd.scissor);
    if (GraphicsRect_isEmpty(clip))
        return true;

    id<MTLBuffer> dstBuf = nil;
    bytes dstOff = 0u;
    if (!mtlResolveBuffer(pool, dst.data, GraphicsImage_byteSize(&dst), &dstBuf, &dstOff))
        return false;

    MtlClearUniform u = {
        .strideBytes = dst.strideBytes,
        .x0 = clip.x0,
        .y0 = clip.y0,
        .x1 = clip.x1,
        .y1 = clip.y1,
        .rgba = cmd.clearColorRGBA,
    };
    id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
    if (enc == nil)
        return false;
    id<MTLComputePipelineState> pipe = MtlGfx_clearPipe(ctx);
    [enc setComputePipelineState:pipe];
    [enc setBuffer:dstBuf offset:(NSUInteger) dstOff atIndex:0];
    [enc setBytes:&u length:sizeof u atIndex:1];
    mtlDispatch2D(enc, pipe, GraphicsRect_width(clip), GraphicsRect_height(clip));
    [enc endEncoding];
    return true;
}

static bool mtlEncodeCopy(MtlGraphicsContext *ctx, const VideoPool *pool,
                          id<MTLCommandBuffer> cmdBuf, GraphicsCommand cmd) {
    GraphicsImage dst = cmd.target;
    GraphicsImage src = cmd.source;
    if (!GraphicsImage_isValid(&dst) || !GraphicsImage_isValid(&src))
        return false;
    if (dst.format != src.format)
        return false;
    u32 bpp = PixelFormat_bytesPerPixel(dst.format);
    if (bpp == 0u)
        return false;

    GraphicsRect dstBounds = GraphicsRect_4(0, 0, (i32) dst.width, (i32) dst.height);
    GraphicsRect placed = GraphicsRect_4(cmd.dstX, cmd.dstY,
                                         cmd.dstX + (i32) src.width,
                                         cmd.dstY + (i32) src.height);
    GraphicsRect clip = GraphicsRect_intersect(dstBounds, placed);
    if (!GraphicsRect_isEmpty(cmd.scissor))
        clip = GraphicsRect_intersect(clip, cmd.scissor);
    if (GraphicsRect_isEmpty(clip))
        return true;

    id<MTLBuffer> dstBuf = nil;
    bytes dstOff = 0u;
    id<MTLBuffer> srcBuf = nil;
    bytes srcOff = 0u;
    if (!mtlResolveBuffer(pool, dst.data, GraphicsImage_byteSize(&dst), &dstBuf, &dstOff) ||
        !mtlResolveBuffer(pool, src.data, GraphicsImage_byteSize(&src), &srcBuf, &srcOff))
        return false;

    MtlCopyUniform u = {
        .dstStrideBytes = dst.strideBytes,
        .srcStrideBytes = src.strideBytes,
        .bpp = bpp,
        .dstX0 = clip.x0,
        .dstY0 = clip.y0,
        .srcX0 = clip.x0 - cmd.dstX,
        .srcY0 = clip.y0 - cmd.dstY,
        .copyW = GraphicsRect_width(clip),
        .copyH = GraphicsRect_height(clip),
    };
    id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
    if (enc == nil)
        return false;
    id<MTLComputePipelineState> pipe = MtlGfx_copyPipe(ctx);
    [enc setComputePipelineState:pipe];
    [enc setBuffer:dstBuf offset:(NSUInteger) dstOff atIndex:0];
    [enc setBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:1];
    [enc setBytes:&u length:sizeof u atIndex:2];
    mtlDispatch2D(enc, pipe, u.copyW, u.copyH);
    [enc endEncoding];
    return true;
}

/**
 * Encodes a custom GPU compute shader dispatch (such as collage_scope.metal
 * blur and blend passes) against MTLBuffer-backed VideoPool slices.
 * @param ctx    Metal graphics context
 * @param pool   backing VideoPool
 * @param cmdBuf active Metal command buffer
 * @param cmd    recorded dispatch command
 * @return true on success, false if shader ID or buffers are invalid
 */
static bool mtlEncodeDispatch(MtlGraphicsContext *ctx, const VideoPool *pool,
                              id<MTLCommandBuffer> cmdBuf, GraphicsCommand cmd) {
    if (cmd.shaderId == 0u || cmd.shaderId > MtlGfx_customCount(ctx))
        return false;
    id<MTLComputePipelineState> pipe = MtlGfx_customPipeAt(ctx, cmd.shaderId - 1u);
    if (pipe == nil)
        return false;

    id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
    if (enc == nil)
        return false;
    [enc setComputePipelineState:pipe];

    if (GraphicsImage_isValid(&cmd.target)) {
        id<MTLBuffer> dstBuf = nil;
        bytes dstOff = 0u;
        if (!mtlResolveBuffer(pool, cmd.target.data, GraphicsImage_byteSize(&cmd.target), &dstBuf, &dstOff)) {
            [enc endEncoding];
            return false;
        }
        [enc setBuffer:dstBuf offset:(NSUInteger) dstOff atIndex:0];
    }
    if (GraphicsImage_isValid(&cmd.source)) {
        id<MTLBuffer> srcBuf = nil;
        bytes srcOff = 0u;
        if (!mtlResolveBuffer(pool, cmd.source.data, GraphicsImage_byteSize(&cmd.source), &srcBuf, &srcOff)) {
            [enc endEncoding];
            return false;
        }
        [enc setBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:1];
    }
    if (cmd.uniformAddr != 0u && cmd.uniformBytes > 0u) {
        id<MTLBuffer> uBuf = nil;
        bytes uOff = 0u;
        if (!mtlResolveBuffer(pool, cmd.uniformAddr, cmd.uniformBytes, &uBuf, &uOff)) {
            [enc endEncoding];
            return false;
        }
        [enc setBuffer:uBuf offset:(NSUInteger) uOff atIndex:2];
    }
    mtlDispatch2D(enc, pipe, cmd.gridX, cmd.gridY);
    [enc endEncoding];
    return true;
}

static fn mtlDrainPendingPresent(MtlGraphicsContext *ctx) {
    if (ctx == nullptr)
        return;
    id<MTLCommandBuffer> pending = MtlGfx_pendingPresent(ctx);
    if (pending == nil)
        return;
    [pending waitUntilCompleted];
    [pending release];
    MtlGfx_setPendingPresent(ctx, nil);
}

static bool mtlSameTarget(GraphicsImage a, GraphicsImage b) {
    return a.data == b.data && a.width == b.width && a.height == b.height && a.strideBytes == b.strideBytes;
}

/**
 * Encodes a contiguous run of GRAPHICS_CMD_DRAW commands targeting the same
 * GraphicsImage inside one Metal render pass.
 * Quirk: blits the VideoPool MTLBuffer into a private MTLTexture before the
 * render pass and blits it back afterward so linear-buffer compute passes and
 * hardware-instanced render passes share the same GraphicsImage storage on GPU.
 * @param ctx       Metal graphics context
 * @param pool      backing VideoPool
 * @param cmdBuf    active Metal command buffer
 * @param cmds      command array
 * @param count     total command count
 * @param startIdx  first GRAPHICS_CMD_DRAW index in the run
 * @param outEndIdx receives the first index after the encoded run
 * @return true on success, false on buffer or pipeline failure
 */
static bool mtlEncodeDrawRun(MtlGraphicsContext *ctx, const VideoPool *pool,
                             id<MTLCommandBuffer> cmdBuf,
                             const GraphicsCommand *cmds, u32 count,
                             u32 startIdx, u32 *outEndIdx) {
    GraphicsImage dst = cmds[startIdx].target;
    if (!GraphicsImage_isValid(&dst))
        return false;

    u32 runEnd = startIdx + 1u;
    while (runEnd < count && cmds[runEnd].kind == GRAPHICS_CMD_DRAW &&
           mtlSameTarget(dst, cmds[runEnd].target)) {
        runEnd++;
    }

    id<MTLBuffer> dstBuf = nil;
    bytes dstOff = 0u;
    if (!mtlResolveBuffer(pool, dst.data, GraphicsImage_byteSize(&dst), &dstBuf, &dstOff))
        return false;

    id<MTLTexture> tex = mtlEnsureRenderTexture(ctx, dst.width, dst.height);
    if (tex == nil)
        return false;

    MTLSize texSize = MTLSizeMake((NSUInteger) dst.width, (NSUInteger) dst.height, 1u);
    MTLOrigin zeroOrigin = MTLOriginMake(0u, 0u, 0u);
    NSUInteger imgBytes = (NSUInteger) GraphicsImage_byteSize(&dst);

    id<MTLBlitCommandEncoder> blitIn = [cmdBuf blitCommandEncoder];
    if (blitIn == nil)
        return false;
    [blitIn copyFromBuffer:dstBuf
              sourceOffset:(NSUInteger) dstOff
         sourceBytesPerRow:(NSUInteger) dst.strideBytes
       sourceBytesPerImage:imgBytes
                sourceSize:texSize
                 toTexture:tex
          destinationSlice:0u
          destinationLevel:0u
         destinationOrigin:zeroOrigin];
    [blitIn endEncoding];

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = tex;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:pass];
    if (enc == nil)
        return false;
    MTLViewport vp = { 0.0, 0.0, (d64) dst.width, (d64) dst.height, 0.0, 1.0 };
    [enc setViewport:vp];
    GraphicsRect bounds = GraphicsRect_4(0, 0, (i32) dst.width, (i32) dst.height);
    MTLScissorRect fullScissor = { 0u, 0u, (NSUInteger) dst.width, (NSUInteger) dst.height };
    id<MTLRenderPipelineState> boundPipe = nil;

    for (u32 j = startIdx; j < runEnd; j++) {
        GraphicsCommand cmd = cmds[j];
        if (cmd.shaderId == 0u || cmd.shaderId > MtlGfx_customCount(ctx) || cmd.drawCount == 0u) {
            [enc endEncoding];
            return false;
        }
        id<MTLRenderPipelineState> pipe = MtlGfx_renderPipeAt(ctx, cmd.shaderId - 1u);
        if (pipe == nil) {
            [enc endEncoding];
            return false;
        }
        id<MTLBuffer> instBuf = nil;
        bytes instOff = 0u;
        if (!mtlResolveBuffer(pool, cmd.vertexAddr, 1u, &instBuf, &instOff)) {
            [enc endEncoding];
            return false;
        }
        id<MTLBuffer> uBuf = nil;
        bytes uOff = 0u;
        if (cmd.uniformAddr != 0u && cmd.uniformBytes > 0u) {
            if (!mtlResolveBuffer(pool, cmd.uniformAddr, cmd.uniformBytes, &uBuf, &uOff)) {
                [enc endEncoding];
                return false;
            }
        }
        id<MTLBuffer> srcBuf = nil;
        bytes srcOff = 0u;
        if (GraphicsImage_isValid(&cmd.source)) {
            if (!mtlResolveBuffer(pool, cmd.source.data, GraphicsImage_byteSize(&cmd.source), &srcBuf, &srcOff)) {
                [enc endEncoding];
                return false;
            }
        }
        if (!GraphicsRect_isEmpty(cmd.scissor)) {
            GraphicsRect clip = GraphicsRect_intersect(bounds, cmd.scissor);
            if (GraphicsRect_isEmpty(clip))
                continue;
            MTLScissorRect sr = {
                (NSUInteger) clip.x0,
                (NSUInteger) clip.y0,
                (NSUInteger) GraphicsRect_width(clip),
                (NSUInteger) GraphicsRect_height(clip),
            };
            [enc setScissorRect:sr];
        } else {
            [enc setScissorRect:fullScissor];
        }
        if (pipe != boundPipe) {
            [enc setRenderPipelineState:pipe];
            boundPipe = pipe;
        }
        [enc setVertexBuffer:instBuf offset:(NSUInteger) instOff atIndex:0];
        [enc setFragmentBuffer:instBuf offset:(NSUInteger) instOff atIndex:0];
        if (uBuf != nil) {
            [enc setVertexBuffer:uBuf offset:(NSUInteger) uOff atIndex:1];
            [enc setFragmentBuffer:uBuf offset:(NSUInteger) uOff atIndex:1];
        }
        if (srcBuf != nil)
            [enc setFragmentBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:2];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0u
                vertexCount:4u
              instanceCount:(NSUInteger) cmd.drawCount];
    }
    [enc endEncoding];

    id<MTLBlitCommandEncoder> blitOut = [cmdBuf blitCommandEncoder];
    if (blitOut == nil)
        return false;
    [blitOut copyFromTexture:tex
                 sourceSlice:0u
                 sourceLevel:0u
                sourceOrigin:zeroOrigin
                  sourceSize:texSize
                    toBuffer:dstBuf
           destinationOffset:(NSUInteger) dstOff
      destinationBytesPerRow:(NSUInteger) dst.strideBytes
    destinationBytesPerImage:imgBytes];
    [blitOut endEncoding];
    *outEndIdx = runEnd;
    return true;
}

/**
 * Encodes and submits all recorded GraphicsCommand entries on the Metal GPU
 * queue, waiting with a bounded 100 ms fence timeout.
 * @param context MtlGraphicsContext handle
 * @param pool    backing VideoPool
 * @param cmds    recorded command list
 * @param count   number of commands to execute
 * @return true on GPU completion, false on timeout, device-lost, or error
 */
static bool mtlSubmitCommands(any context, const VideoPool *pool,
                              const GraphicsCommand *cmds, u32 count) {
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx == nullptr || pool == nullptr)
        return false;
    if (!MtlGuard_check("mtlSubmitCommands", MtlGfx_device(ctx), MtlGfx_queue(ctx), MtlGfx_deviceLostPtr(ctx)))
        return false;
    if (count == 0u)
        return true;

    mtlDrainPendingPresent(ctx);

    @autoreleasepool {
        id<MTLCommandBuffer> cmdBuf = [MtlGfx_queue(ctx) commandBuffer];
        if (cmdBuf == nil)
            return false;

        for (u32 i = 0u; i < count; i++) {
            GraphicsCommand c = cmds[i];
            switch (c.kind) {
                case GRAPHICS_CMD_CLEAR:
                    if (!mtlEncodeClear(ctx, pool, cmdBuf, c))
                        return false;
                    break;
                case GRAPHICS_CMD_COPY:
                    if (!mtlEncodeCopy(ctx, pool, cmdBuf, c))
                        return false;
                    break;
                case GRAPHICS_CMD_DISPATCH:
                    if (!mtlEncodeDispatch(ctx, pool, cmdBuf, c))
                        return false;
                    break;
                case GRAPHICS_CMD_DRAW: {
                    u32 runEnd = i + 1u;
                    if (!mtlEncodeDrawRun(ctx, pool, cmdBuf, cmds, count, i, &runEnd))
                        return false;
                    i = runEnd - 1u;
                    break;
                }
                case GRAPHICS_CMD_PRESENT:
                    break;
                default:
                    return false;
            }
        }
        bool done = mtlWaitBounded(ctx, cmdBuf);
        id<MTLTexture> rtex = MtlGfx_renderTex(ctx);
        if (rtex != nil)
            (discard) [rtex setPurgeableState:MTLPurgeableStateVolatile];
        return done;
    }
}

static d64 mtlSrgbByteToLinear(u8 v) {
    f32 c = (f32) v / 255.0f;
    f32 lin = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    return (d64) lin;
}

static MTLClearColor mtlClearColorFromRGBA(u32 rgba) {
    return MTLClearColorMake(
        mtlSrgbByteToLinear(ColorRGBA_red(rgba)),
        mtlSrgbByteToLinear(ColorRGBA_green(rgba)),
        mtlSrgbByteToLinear(ColorRGBA_blue(rgba)),
        (d64) ColorRGBA_alpha(rgba) / 255.0);
}

static CAMetalLayer *mtlResolveSurfaceLayer(id<MTLDevice> dev, any nativeSurface,
                                            u32 viewportW, u32 viewportH,
                                            u32 surfaceW, u32 surfaceH,
                                            bool forceSync, bool *outUseSync) {
    if (outUseSync != nullptr)
        *outUseSync = false;
    id obj = (id) nativeSurface;
    if (obj == nil)
        return nil;
    u32 drawW = surfaceW >= viewportW ? surfaceW : viewportW;
    u32 drawH = surfaceH >= viewportH ? surfaceH : viewportH;
    CAMetalLayer *layer = nil;
    bool liveResize = false;
    if ([obj isKindOfClass:[CAMetalLayer class]]) {
        layer = (CAMetalLayer*) obj;
    } else if ([obj isKindOfClass:[NSView class]]) {
        NSView *view = (NSView*) obj;
        [view setWantsLayer:YES];
        CALayer *host = [view layer];
        if (host == nil)
            return nil;
        if ([host isKindOfClass:[CAMetalLayer class]]) {
            layer = (CAMetalLayer*) host;
        } else {
            [host setMasksToBounds:YES];
            for (CALayer *sub in [host sublayers]) {
                if ([sub isKindOfClass:[CAMetalLayer class]] &&
                    [[sub name] isEqualToString:@"graphvex.metal.surface"]) {
                    layer = (CAMetalLayer*) sub;
                    break;
                }
            }
            if (layer == nil) {
                layer = [CAMetalLayer layer];
                [layer setName:@"graphvex.metal.surface"];
                [layer setAutoresizingMask:0];
                [layer setContentsGravity:kCAGravityTopLeft];
                [host addSublayer:layer];
            }
        }
        NSWindow *win = [view window];
        liveResize = [view inLiveResize] || (win != nil && [win inLiveResize]);
        CGFloat effectiveScale = (win != nil && [win backingScaleFactor] > 0.0)
            ? [win backingScaleFactor]
            : 1.0;
        CGRect targetFrame = CGRectMake(
            0.0, 0.0,
            (CGFloat) drawW / effectiveScale,
            (CGFloat) drawH / effectiveScale);
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [layer setFrame:targetFrame];
        [layer setContentsScale:effectiveScale];
        [layer setContentsGravity:kCAGravityTopLeft];
        [CATransaction commit];
    } else {
        return nil;
    }
    bool useSync = liveResize || forceSync;
    if (layer.device != dev)
        layer.device = dev;
    if (layer.pixelFormat != MTLPixelFormatBGRA8Unorm)
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    if (layer.framebufferOnly)
        layer.framebufferOnly = NO;
    if (layer.maximumDrawableCount != 3)
        layer.maximumDrawableCount = 3;
    layer.presentsWithTransaction = useSync ? YES : NO;
    CGSize curSize = layer.drawableSize;
    if (curSize.width != (CGFloat) drawW || curSize.height != (CGFloat) drawH)
        layer.drawableSize = CGSizeMake((CGFloat) drawW, (CGFloat) drawH);
    if (outUseSync != nullptr)
        *outUseSync = useSync;
    return layer;
}

static bool mtlFlushSurfaceClear(id<MTLCommandBuffer> cmdBuf,
                                 id<MTLTexture> srgbView, u32 clearRGBA) {
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = srgbView;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = mtlClearColorFromRGBA(clearRGBA);
    id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:pass];
    if (enc == nil)
        return false;
    [enc endEncoding];
    return true;
}

static bool mtlEncodeSurfaceCopy(MtlGraphicsContext *ctx, const VideoPool *pool,
                                 id<MTLCommandBuffer> cmdBuf, id<MTLTexture> rawTex,
                                 u32 viewportW, u32 viewportH, GraphicsCommand cmd) {
    GraphicsImage src = cmd.source;
    if (!GraphicsImage_isValid(&src))
        return false;
    if (PixelFormat_bytesPerPixel(src.format) != 4u)
        return false;
    GraphicsRect dstBounds = GraphicsRect_4(0, 0, (i32) viewportW, (i32) viewportH);
    GraphicsRect placed = GraphicsRect_4(cmd.dstX, cmd.dstY,
                                         cmd.dstX + (i32) src.width,
                                         cmd.dstY + (i32) src.height);
    GraphicsRect clip = GraphicsRect_intersect(dstBounds, placed);
    if (!GraphicsRect_isEmpty(cmd.scissor))
        clip = GraphicsRect_intersect(clip, cmd.scissor);
    if (GraphicsRect_isEmpty(clip))
        return true;

    id<MTLBuffer> srcBuf = nil;
    bytes srcOff = 0u;
    if (!mtlResolveBuffer(pool, src.data, GraphicsImage_byteSize(&src), &srcBuf, &srcOff))
        return false;

    MtlCopyUniform u = {
        .dstStrideBytes = 0u,
        .srcStrideBytes = src.strideBytes,
        .bpp = 4u,
        .dstX0 = clip.x0,
        .dstY0 = clip.y0,
        .srcX0 = clip.x0 - cmd.dstX,
        .srcY0 = clip.y0 - cmd.dstY,
        .copyW = GraphicsRect_width(clip),
        .copyH = GraphicsRect_height(clip),
    };
    id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
    if (enc == nil)
        return false;
    id<MTLComputePipelineState> pipe = MtlGfx_copyToTexPipe(ctx);
    [enc setComputePipelineState:pipe];
    [enc setBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:0];
    [enc setTexture:rawTex atIndex:0];
    [enc setBytes:&u length:sizeof u atIndex:1];
    mtlDispatch2D(enc, pipe, u.copyW, u.copyH);
    [enc endEncoding];
    return true;
}

static bool mtlEncodeSurfaceDrawRun(MtlGraphicsContext *ctx, const VideoPool *pool,
                                    id<MTLCommandBuffer> cmdBuf, id<MTLTexture> srgbView,
                                    u32 viewportW, u32 viewportH,
                                    bool hasClear, u32 clearRGBA,
                                    const GraphicsCommand *cmds, u32 count,
                                    u32 startIdx, u32 *outEndIdx) {
    u32 runEnd = startIdx + 1u;
    while (runEnd < count && cmds[runEnd].kind == GRAPHICS_CMD_DRAW)
        runEnd++;

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = srgbView;
    pass.colorAttachments[0].loadAction = hasClear ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (hasClear)
        pass.colorAttachments[0].clearColor = mtlClearColorFromRGBA(clearRGBA);

    id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:pass];
    if (enc == nil)
        return false;
    MTLViewport vp = { 0.0, 0.0, (d64) viewportW, (d64) viewportH, 0.0, 1.0 };
    [enc setViewport:vp];
    GraphicsRect bounds = GraphicsRect_4(0, 0, (i32) viewportW, (i32) viewportH);
    MTLScissorRect fullScissor = { 0u, 0u, (NSUInteger) viewportW, (NSUInteger) viewportH };
    id<MTLRenderPipelineState> boundPipe = nil;

    for (u32 j = startIdx; j < runEnd; j++) {
        GraphicsCommand cmd = cmds[j];
        if (cmd.shaderId == 0u || cmd.shaderId > MtlGfx_customCount(ctx) || cmd.drawCount == 0u) {
            [enc endEncoding];
            return false;
        }
        id<MTLRenderPipelineState> pipe = MtlGfx_surfaceRenderPipeAt(ctx, cmd.shaderId - 1u);
        if (pipe == nil) {
            [enc endEncoding];
            return false;
        }
        id<MTLBuffer> instBuf = nil;
        bytes instOff = 0u;
        if (!mtlResolveBuffer(pool, cmd.vertexAddr, 1u, &instBuf, &instOff)) {
            [enc endEncoding];
            return false;
        }
        id<MTLBuffer> uBuf = nil;
        bytes uOff = 0u;
        if (cmd.uniformAddr != 0u && cmd.uniformBytes > 0u) {
            if (!mtlResolveBuffer(pool, cmd.uniformAddr, cmd.uniformBytes, &uBuf, &uOff)) {
                [enc endEncoding];
                return false;
            }
        }
        id<MTLBuffer> srcBuf = nil;
        bytes srcOff = 0u;
        if (GraphicsImage_isValid(&cmd.source)) {
            if (!mtlResolveBuffer(pool, cmd.source.data, GraphicsImage_byteSize(&cmd.source), &srcBuf, &srcOff)) {
                [enc endEncoding];
                return false;
            }
        }
        if (!GraphicsRect_isEmpty(cmd.scissor)) {
            GraphicsRect clip = GraphicsRect_intersect(bounds, cmd.scissor);
            if (GraphicsRect_isEmpty(clip))
                continue;
            MTLScissorRect sr = {
                (NSUInteger) clip.x0,
                (NSUInteger) clip.y0,
                (NSUInteger) GraphicsRect_width(clip),
                (NSUInteger) GraphicsRect_height(clip),
            };
            [enc setScissorRect:sr];
        } else {
            [enc setScissorRect:fullScissor];
        }
        if (pipe != boundPipe) {
            [enc setRenderPipelineState:pipe];
            boundPipe = pipe;
        }
        [enc setVertexBuffer:instBuf offset:(NSUInteger) instOff atIndex:0];
        [enc setFragmentBuffer:instBuf offset:(NSUInteger) instOff atIndex:0];
        if (uBuf != nil) {
            [enc setVertexBuffer:uBuf offset:(NSUInteger) uOff atIndex:1];
            [enc setFragmentBuffer:uBuf offset:(NSUInteger) uOff atIndex:1];
        }
        if (srcBuf != nil)
            [enc setFragmentBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:2];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0u
                vertexCount:4u
              instanceCount:(NSUInteger) cmd.drawCount];
    }
    [enc endEncoding];
    *outEndIdx = runEnd;
    return true;
}

static fn mtlReleaseSwapchain(MtlGraphicsContext *ctx) {
    if (ctx == nullptr)
        return;
    for (u32 i = 0u; i < MTL_SWAP_COUNT; i++) {
        id<MTLTexture> sv = MtlGfx_swapSrgbAt(ctx, i);
        if (sv != nil) {
            [sv release];
            MtlGfx_setSwapSrgbAt(ctx, i, nil);
        }
        id<MTLTexture> t = MtlGfx_swapTexAt(ctx, i);
        if (t != nil) {
            [t release];
            MtlGfx_setSwapTexAt(ctx, i, nil);
        }
        IOSurfaceRef s = MtlGfx_swapSurfaceAt(ctx, i);
        if (s != nullptr) {
            CFRelease(s);
            MtlGfx_setSwapSurfaceAt(ctx, i, nullptr);
        }
    }
    MtlGfx_setSwapSize(ctx, 0u, 0u);
    MtlGfx_setSwapIndex(ctx, 0u);
}

static bool mtlEnsureSwapchain(MtlGraphicsContext *ctx, u32 width, u32 height) {
    if (ctx == nullptr || width == 0u || height == 0u)
        return false;

    if (MtlGfx_swapW(ctx) == width && MtlGfx_swapH(ctx) == height) {
        bool allReady = true;
        for (u32 i = 0u; i < MTL_SWAP_COUNT; i++) {
            if (MtlGfx_swapSurfaceAt(ctx, i) == nullptr)
                allReady = false;
        }
        if (allReady)
            return true;
    }
    mtlReleaseSwapchain(ctx);

    u32 rowBytes = ((width * 4u) + 63u) & ~63u;
    u32 pixelFmt = 0x42475241u;
    NSDictionary *props = @{
        (id) kIOSurfaceWidth: @(width),
        (id) kIOSurfaceHeight: @(height),
        (id) kIOSurfaceBytesPerElement: @(4u),
        (id) kIOSurfaceBytesPerRow: @(rowBytes),
        (id) kIOSurfacePixelFormat: @(pixelFmt),
    };
    MTLTextureDescriptor *desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:(NSUInteger) width
                                    height:(NSUInteger) height
                                 mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite;

    id<MTLDevice> dev = MtlGfx_device(ctx);
    for (u32 i = 0u; i < MTL_SWAP_COUNT; i++) {
        IOSurfaceRef surf = IOSurfaceCreate((CFDictionaryRef) props);
        if (surf == nullptr) {
            mtlReleaseSwapchain(ctx);
            return false;
        }
        id<MTLTexture> tex = [dev newTextureWithDescriptor:desc iosurface:surf plane:0u];
        if (tex == nil) {
            CFRelease(surf);
            mtlReleaseSwapchain(ctx);
            return false;
        }
        id<MTLTexture> srgb = [tex newTextureViewWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB];
        if (srgb == nil) {
            [tex release];
            CFRelease(surf);
            mtlReleaseSwapchain(ctx);
            return false;
        }
        MtlGfx_setSwapSurfaceAt(ctx, i, surf);
        MtlGfx_setSwapTexAt(ctx, i, tex);
        MtlGfx_setSwapSrgbAt(ctx, i, srgb);
    }
    MtlGfx_setSwapSize(ctx, width, height);
    MtlGfx_setSwapIndex(ctx, 0u);
    return true;
}

static u32 mtlPickSwapSlot(const MtlGraphicsContext *ctx)
{
    u32 base = MtlGfx_swapIndex(ctx) % MTL_SWAP_COUNT;
    for (u32 i = 0u; i < MTL_SWAP_COUNT; i++) {
        u32 idx = (base + i) % MTL_SWAP_COUNT;
        IOSurfaceRef s = MtlGfx_swapSurfaceAt(ctx, idx);
        if (s != nullptr && !IOSurfaceIsInUse(s))
            return idx;
    }
    return base;
}

static CALayer *mtlResolvePresentLayer(any nativeSurface,
                                       u32 drawW, u32 drawH,
                                       bool forceSync, bool *outUseSync,
                                       CGRect *outFrame, CGFloat *outScale,
                                       bool *outUpdateGeom)
{
    if (outUseSync != nullptr)
        *outUseSync = false;

    if (outUpdateGeom != nullptr)
        *outUpdateGeom = false;

    id obj = (id) nativeSurface;
    if (obj == nil)
        return nil;

    CALayer *layer = nil;
    bool liveResize = false;
    CGFloat backingScale = 1.0;
    if ([obj isKindOfClass:[NSView class]]) {
        NSView *view = (NSView*) obj;
        [view setWantsLayer:YES];
        CALayer *host = [view layer];
        if (host == nil)
            return nil;

        [host setMasksToBounds:YES];
        for (CALayer *sub in [host sublayers]) {
            if ([[sub name] isEqualToString:@"graphvex.iosurface.surface"]) {
                layer = sub;
            } else if ([[sub name] isEqualToString:@"graphvex.metal.surface"]) {
                [sub setHidden:YES];
            }
        }
        if (layer == nil) {
            layer = [CALayer layer];
            [layer setName:@"graphvex.iosurface.surface"];
            [layer setOpaque:YES];
            [layer setAutoresizingMask:0];
            [layer setContentsGravity:kCAGravityTopLeft];
            [host addSublayer:layer];
        }
        NSWindow *win = [view window];
        liveResize = [view inLiveResize] || (win != nil && [win inLiveResize]);
        if (win != nil && [win backingScaleFactor] > 0.0)
            backingScale = [win backingScaleFactor];

        CGRect targetFrame = CGRectMake(
            0.0, 0.0,
            (CGFloat) drawW / backingScale,
            (CGFloat) drawH / backingScale);
        if (outFrame != nullptr)
            *outFrame = targetFrame;

        if (outScale != nullptr)
            *outScale = backingScale;

        if (outUpdateGeom != nullptr)
            *outUpdateGeom = true;
    } else if ([obj isKindOfClass:[CALayer class]]) {
        layer = (CALayer*) obj;
    } else {
        return nil;
    }
    if (outUseSync != nullptr)
        *outUseSync = liveResize || forceSync;

    return layer;
}

static fn mtlCommitPresentLayer(CALayer *layer, IOSurfaceRef surf,
                                bool updateGeom, CGRect targetFrame,
                                CGFloat targetScale, bool useSync)
{
    if (layer == nil || surf == nullptr)
        return;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (updateGeom) {
        if ([layer isHidden])
            [layer setHidden:NO];

        if (!CGRectEqualToRect([layer frame], targetFrame))
            [layer setFrame:targetFrame];

        if ([layer contentsScale] != targetScale)
            [layer setContentsScale:targetScale];

        [layer setContentsGravity:kCAGravityTopLeft];
    }
    [layer setContents:(id) surf];
    [CATransaction commit];
    if (useSync)
        [CATransaction flush];
}

/**
 * Renders all recorded commands directly on the Metal GPU into a custom
 * 3-IOSurface screen-sized swapchain and swaps CALayer.contents atomically
 * with layer geometry inside a CATransaction (flushing synchronously during
 * live resize and committing asynchronously at rest).
 * @param context       MtlGraphicsContext handle
 * @param pool          backing VideoPool for primitive and atlas buffers
 * @param nativeSurface NSView or CALayer target
 * @param viewportW     active window viewport width in pixels
 * @param viewportH     active window viewport height in pixels
 * @param surfaceW      fixed backing IOSurface width in pixels
 * @param surfaceH      fixed backing IOSurface height in pixels
 * @param cmds          recorded command list
 * @param count         number of commands to execute
 * @return true on success, false on IOSurface, shader, or GPU failure
 */
static fn mtlSetSurfacePurgeable(any context, bool purgeable);

static bool mtlSubmitToSurface(any context, const VideoPool *pool,
                               any nativeSurface,
                               u32 viewportW, u32 viewportH,
                               u32 surfaceW, u32 surfaceH,
                               const GraphicsCommand *cmds, u32 count) {
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx == nullptr || pool == nullptr || nativeSurface == nullptr)
        return false;

    if (!MtlGuard_check("mtlSubmitToSurface", MtlGfx_device(ctx), MtlGfx_queue(ctx), MtlGfx_deviceLostPtr(ctx)))
        return false;

    u32 drawW = surfaceW >= viewportW ? surfaceW : viewportW;
    u32 drawH = surfaceH >= viewportH ? surfaceH : viewportH;

    mtlDrainPendingPresent(ctx);

    @autoreleasepool {
        if (!mtlEnsureSwapchain(ctx, drawW, drawH))
            return false;

        if (MtlGfx_surfacePurgeable(ctx))
            mtlSetSurfacePurgeable(ctx, false);

        bool useSync = false;
        CGRect targetFrame = CGRectZero;
        CGFloat targetScale = 1.0;
        bool updateGeom = false;
        CALayer *layer = mtlResolvePresentLayer(
            nativeSurface, drawW, drawH,
            MtlGfx_syncPresent(ctx), &useSync,
            &targetFrame, &targetScale, &updateGeom);
        if (layer == nil)
            return false;

        u32 slot = mtlPickSwapSlot(ctx);
        IOSurfaceRef surf = MtlGfx_swapSurfaceAt(ctx, slot);
        id<MTLTexture> rawTex = MtlGfx_swapTexAt(ctx, slot);
        id<MTLTexture> srgbView = MtlGfx_swapSrgbAt(ctx, slot);
        if (surf == nullptr || rawTex == nil || srgbView == nil)
            return false;

        id<MTLCommandBuffer> cmdBuf = [MtlGfx_queue(ctx) commandBuffer];
        if (cmdBuf == nil)
            return false;

        bool hasClear = false;
        u32 clearRGBA = 0u;
        bool ok = true;
        for (u32 i = 0u; i < count && ok; i++) {
            GraphicsCommand c = cmds[i];
            switch (c.kind) {
                case GRAPHICS_CMD_CLEAR:
                    hasClear = true;
                    clearRGBA = c.clearColorRGBA;
                    break;
                case GRAPHICS_CMD_DRAW: {
                    u32 runEnd = i + 1u;
                    ok = mtlEncodeSurfaceDrawRun(
                        ctx, pool, cmdBuf, srgbView, viewportW, viewportH,
                        hasClear, clearRGBA, cmds, count, i, &runEnd);
                    hasClear = false;
                    i = runEnd - 1u;
                    break;
                }
                case GRAPHICS_CMD_COPY:
                    if (hasClear) {
                        ok = mtlFlushSurfaceClear(cmdBuf, srgbView, clearRGBA);
                        hasClear = false;
                    }
                    if (ok)
                        ok = mtlEncodeSurfaceCopy(ctx, pool, cmdBuf, rawTex, viewportW, viewportH, c);
                    break;
                case GRAPHICS_CMD_DISPATCH:
                    if (hasClear) {
                        ok = mtlFlushSurfaceClear(cmdBuf, srgbView, clearRGBA);
                        hasClear = false;
                    }
                    if (ok)
                        ok = mtlEncodeDispatch(ctx, pool, cmdBuf, c);
                    break;
                case GRAPHICS_CMD_PRESENT:
                    break;
                default:
                    ok = false;
                    break;
            }
        }
        if (ok && hasClear)
            ok = mtlFlushSurfaceClear(cmdBuf, srgbView, clearRGBA);

        if (!ok)
            return false;

        if (!mtlWaitBounded(ctx, cmdBuf))
            return false;

        mtlCommitPresentLayer(layer, surf, updateGeom, targetFrame, targetScale, useSync);
        MtlGfx_setSwapIndex(ctx, (slot + 1u) % MTL_SWAP_COUNT);
        return true;
    }
}

static bool mtlSurfacePrefersMetalLayer(any nativeSurface) {
    id obj = (id) nativeSurface;
    if (obj == nil)
        return false;

    if ([obj isKindOfClass:[CAMetalLayer class]])
        return true;

    if ([obj isKindOfClass:[NSView class]]) {
        NSView *view = (NSView*) obj;
        CALayer *host = [view layer];
        if (host != nil && [host isKindOfClass:[CAMetalLayer class]])
            return true;
    }

    return false;
}

;;INTENTION("NSView targets share the single 2-IOSurface CALayer swapchain with mtlSubmitToSurface so switching between direct and offscreen-filtered frames never leaves a stale CAMetalLayer occluding the window")
/**
 * Presents a VideoPool RGBA8 GraphicsImage onto the shared 2-IOSurface CALayer
 * swapchain (for NSView/CALayer surfaces) or a CAMetalLayer drawable on the
 * GPU via the graphvex_present_rgba8 compute kernel (handling RGBA to BGRA
 * conversion on GPU with zero CPU pixel copies).
 * @param context       MtlGraphicsContext handle
 * @param pool          backing VideoPool
 * @param nativeSurface NSView, CALayer, or CAMetalLayer target (nullptr is a no-op)
 * @param image         rendered RGBA8 GraphicsImage to display
 * @return true on success, false if swapchain/drawable acquisition or dispatch fails
 */
static bool mtlPresentImage(any context, const VideoPool *pool,
                            any nativeSurface, const GraphicsImage *image) {
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx == nullptr || pool == nullptr || !GraphicsImage_isValid(image))
        return false;

    if (!MtlGuard_check("mtlPresentImage", MtlGfx_device(ctx), MtlGfx_queue(ctx), MtlGfx_deviceLostPtr(ctx)))
        return false;

    if (nativeSurface == nullptr)
        return true;

    @autoreleasepool {
        u32 imgW = GraphicsImage_width(image);
        u32 imgH = GraphicsImage_height(image);
        id<MTLBuffer> srcBuf = nil;
        bytes srcOff = 0u;
        if (!mtlResolveBuffer(pool, GraphicsImage_data(image), GraphicsImage_byteSize(image), &srcBuf, &srcOff))
            return false;

        if (!mtlSurfacePrefersMetalLayer(nativeSurface)) {
            mtlDrainPendingPresent(ctx);
            u32 curSwapW = MtlGfx_swapW(ctx);
            u32 curSwapH = MtlGfx_swapH(ctx);
            u32 drawW = curSwapW >= imgW ? curSwapW : imgW;
            u32 drawH = curSwapH >= imgH ? curSwapH : imgH;
            if (!mtlEnsureSwapchain(ctx, drawW, drawH))
                return false;

            if (MtlGfx_surfacePurgeable(ctx))
                mtlSetSurfacePurgeable(ctx, false);

            bool useSync = false;
            CGRect targetFrame = CGRectZero;
            CGFloat targetScale = 1.0;
            bool updateGeom = false;
            CALayer *layer = mtlResolvePresentLayer(
                nativeSurface, drawW, drawH,
                MtlGfx_syncPresent(ctx), &useSync,
                &targetFrame, &targetScale, &updateGeom);
            if (layer == nil)
                return false;

            u32 slot = mtlPickSwapSlot(ctx);
            IOSurfaceRef surf = MtlGfx_swapSurfaceAt(ctx, slot);
            id<MTLTexture> rawTex = MtlGfx_swapTexAt(ctx, slot);
            if (surf == nullptr || rawTex == nil)
                return false;

            MtlPresentUniform u;
            memset(&u, 0, sizeof(MtlPresentUniform));
            u.srcWidth = imgW;
            u.srcHeight = imgH;
            u.srcStrideBytes = GraphicsImage_strideBytes(image);
            u.dstWidth = drawW;
            u.dstHeight = drawH;

            id<MTLCommandBuffer> cmdBuf = [MtlGfx_queue(ctx) commandBuffer];
            if (cmdBuf == nil)
                return false;

            id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
            if (enc == nil)
                return false;

            id<MTLComputePipelineState> pipe = MtlGfx_presentPipe(ctx);
            [enc setComputePipelineState:pipe];
            [enc setBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:0];
            [enc setTexture:rawTex atIndex:0];
            [enc setBytes:&u length:sizeof u atIndex:1];
            mtlDispatch2D(enc, pipe, u.dstWidth, u.dstHeight);
            [enc endEncoding];

            if (!mtlWaitBounded(ctx, cmdBuf))
                return false;

            mtlCommitPresentLayer(layer, surf, updateGeom, targetFrame, targetScale, useSync);
            MtlGfx_setSwapIndex(ctx, (slot + 1u) % MTL_SWAP_COUNT);
            return true;
        }

        bool useSync = false;
        CAMetalLayer *layer = mtlResolveSurfaceLayer(
            MtlGfx_device(ctx), nativeSurface,
            imgW, imgH, imgW, imgH,
            MtlGfx_syncPresent(ctx), &useSync);
        if (layer == nil)
            return false;

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (drawable == nil)
            return false;

        id<MTLTexture> tex = drawable.texture;
        MtlPresentUniform u;
        memset(&u, 0, sizeof(MtlPresentUniform));
        u.srcWidth = imgW;
        u.srcHeight = imgH;
        u.srcStrideBytes = GraphicsImage_strideBytes(image);
        u.dstWidth = (u32) tex.width;
        u.dstHeight = (u32) tex.height;

        id<MTLCommandBuffer> cmdBuf = [MtlGfx_queue(ctx) commandBuffer];
        if (cmdBuf == nil)
            return false;

        id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
        if (enc == nil)
            return false;

        id<MTLComputePipelineState> pipe = MtlGfx_presentPipe(ctx);
        [enc setComputePipelineState:pipe];
        [enc setBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:0];
        [enc setTexture:tex atIndex:0];
        [enc setBytes:&u length:sizeof u atIndex:1];
        mtlDispatch2D(enc, pipe, u.dstWidth, u.dstHeight);
        [enc endEncoding];

        if (useSync) {
            if (!mtlWaitBounded(ctx, cmdBuf))
                return false;
            [drawable present];
            [CATransaction flush];
            return true;
        }

        mtlDrainPendingPresent(ctx);
        [cmdBuf presentDrawable:drawable];
        [cmdBuf commit];
        [cmdBuf retain];
        MtlGfx_setPendingPresent(ctx, cmdBuf);
        return true;
    }
}

static fn mtlSetSyncPresent(any context, bool sync) {
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx != nullptr)
        MtlGfx_setSyncPresent(ctx, sync);
}

static fn mtlSetSurfacePurgeable(any context, bool purgeable)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (!ctx)
        return;

    mtlDrainPendingPresent(ctx);
    u32 state = purgeable ? kIOSurfacePurgeableVolatile : kIOSurfacePurgeableNonVolatile;
    for (u32 i = 0u; i < MTL_SWAP_COUNT; i++) {
        IOSurfaceRef surf = MtlGfx_swapSurfaceAt(ctx, i);
        if (surf != nullptr)
            (discard) IOSurfaceSetPurgeable(surf, state, nullptr);
    }
    MtlGfx_setSurfacePurgeable(ctx, purgeable);
}

;;DECONSTRUCT
static fn mtlDestroyGraphicsContext(any context) {
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx == nullptr)
        return;
    SecureHandle sh = MtlGfx_selfHandle(ctx);
    mtlDrainPendingPresent(ctx);
    u32 nCustom = MtlGfx_customCount(ctx);
    for (u32 i = 0u; i < nCustom; i++) {
        id<MTLComputePipelineState> p = MtlGfx_customPipeAt(ctx, i);
        if (p != nil)
            [p release];
        id<MTLRenderPipelineState> rp = MtlGfx_renderPipeAt(ctx, i);
        if (rp != nil)
            [rp release];
        id<MTLRenderPipelineState> srp = MtlGfx_surfaceRenderPipeAt(ctx, i);
        if (srp != nil)
            [srp release];
    }
    mtlReleaseSwapchain(ctx);
    if (MtlGfx_renderTex(ctx) != nil)
        [MtlGfx_renderTex(ctx) release];
    if (MtlGfx_presentPipe(ctx) != nil)
        [MtlGfx_presentPipe(ctx) release];
    if (MtlGfx_copyToTexPipe(ctx) != nil)
        [MtlGfx_copyToTexPipe(ctx) release];
    if (MtlGfx_copyPipe(ctx) != nil)
        [MtlGfx_copyPipe(ctx) release];
    if (MtlGfx_clearPipe(ctx) != nil)
        [MtlGfx_clearPipe(ctx) release];
    if (MtlGfx_library(ctx) != nil)
        [MtlGfx_library(ctx) release];
    u32 nCached = MtlGfx_cachedLibCount(ctx);
    for (u32 i = 0u; i < nCached; i++) {
        id<MTLLibrary> clib = MtlGfx_cachedLibAt(ctx, i);
        if (clib != nil)
            [clib release];
    }
    if (MtlGfx_ownsDevice(ctx)) {
        if (MtlGfx_queue(ctx) != nil)
            [MtlGfx_queue(ctx) release];
        if (MtlGfx_device(ctx) != nil)
            [MtlGfx_device(ctx) release];
    }
    if (SecureHandle_isValid(sh))
        (discard) SecureMemoryArena_free(Graphvex_secureArena(), sh);
}

static id<MTLComputePipelineState> mtlBuildPipeline(id<MTLDevice> dev,
                                                    id<MTLLibrary> lib,
                                                    string fnName) {
    NSString *name = [NSString stringWithUTF8String:fnName];
    id<MTLFunction> func = [lib newFunctionWithName:name];
    if (func == nil) {
        fprint(stderr, "mtlBuildPipeline: missing function ", fnName);
        return nil;
    }
    NSError *err = nil;
    id<MTLComputePipelineState> pipe = [dev newComputePipelineStateWithFunction:func error:&err];
    [func release];
    if (pipe == nil) {
        string desc = err != nil ? (string) [[err localizedDescription] UTF8String] : "unknown error";
        fprint(stderr, "mtlBuildPipeline: pipeline failed for ", fnName, ": ", desc);
    }
    return pipe;
}

static id<MTLRenderPipelineState> mtlBuildRenderPipeline(id<MTLDevice> dev,
                                                         id<MTLLibrary> lib,
                                                         string entryPair,
                                                         MTLPixelFormat pixelFormat) {
    string colon = nullptr;
    for (string p = entryPair; *p != '\0'; p++) {
        if (*p == ':') {
            colon = p;
            break;
        }
    }
    if (colon == nullptr || colon == entryPair || colon[1] == '\0')
        return nil;

    NSString *vertName = [[NSString alloc] initWithBytes:entryPair
                                                  length:(NSUInteger) (colon - entryPair)
                                                encoding:NSUTF8StringEncoding];
    NSString *fragName = [NSString stringWithUTF8String:(colon + 1)];
    id<MTLFunction> vertFn = [lib newFunctionWithName:vertName];
    id<MTLFunction> fragFn = [lib newFunctionWithName:fragName];
    [vertName release];
    if (vertFn == nil || fragFn == nil) {
        fprint(stderr, "mtlBuildRenderPipeline: missing function in ", entryPair);
        if (vertFn != nil)
            [vertFn release];
        if (fragFn != nil)
            [fragFn release];
        return nil;
    }

    MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vertFn;
    desc.fragmentFunction = fragFn;
    desc.colorAttachments[0].pixelFormat = pixelFormat;
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    NSError *err = nil;
    id<MTLRenderPipelineState> pipe = [dev newRenderPipelineStateWithDescriptor:desc error:&err];
    [desc release];
    [vertFn release];
    [fragFn release];
    if (pipe == nil) {
        string descStr = err != nil ? (string) [[err localizedDescription] UTF8String] : "unknown error";
        fprint(stderr, "mtlBuildRenderPipeline: pipeline failed for ", entryPair, ": ", descStr);
    }
    return pipe;
}

static bool mtlPathEquals(string a, string b)
{
    if (!a || !b)
        return false;

    u32 i = 0u;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i])
            return false;
        i++;
    }
    return a[i] == b[i];
}

static id<MTLLibrary> mtlAcquireLibrary(MtlGraphicsContext *ctx, string path, bool *outOwned)
{
    if (!ctx || !path || !outOwned)
        return nil;

    u32 nCached = MtlGfx_cachedLibCount(ctx);
    for (u32 i = 0u; i < nCached; i++) {
        if (mtlPathEquals(MtlGfx_cachedLibPathAt(ctx, i), path)) {
            *outOwned = false;
            return MtlGfx_cachedLibAt(ctx, i);
        }
    }

    ShaderBytes loaded = Shader_loadFile(path);
    if (!ShaderBytes_isValid(&loaded))
        return nil;

    NSError *err = nil;
    NSString *src = [NSString stringWithUTF8String:(string) ShaderBytes_data(&loaded)];
    id<MTLLibrary> lib = [MtlGfx_device(ctx) newLibraryWithSource:src options:nil error:&err];
    ShaderBytes_destroy(&loaded);
    if (lib == nil) {
        string desc = err != nil ? (string) [[err localizedDescription] UTF8String] : "unknown error";
        fprint(stderr, "mtlRegisterShader: MSL compile failed for ", path, ": ", desc);
        return nil;
    }

    if (nCached < MTL_MAX_CACHED_LIBS) {
        MtlGfx_setCachedLibAt(ctx, nCached, lib);
        MtlGfx_setCachedLibPathAt(ctx, nCached, path);
        MtlGfx_setCachedLibCount(ctx, nCached + 1u);
        *outOwned = false;
        return lib;
    }

    *outOwned = true;
    return lib;
}

/**
 * Reads an MSL shader file from disk (with per-context MTLLibrary caching),
 * compiles it into a Metal compute or render pipeline state, and stores it in
 * MtlGraphicsContext.
 * Cold path: callers cache the returned shader ID per Graphics context so
 * disk I/O and MSL compilation happen once rather than per frame.
 * @param context   MtlGraphicsContext handle
 * @param path      relative path to the .metal source file
 * @param entryName compute kernel name, or "vert:frag" pair for render stage 0
 * @param stage     0 for hardware render pipeline, non-zero for compute kernel
 * @return 1-based shader ID on success, or 0 on load, compile, or capacity failure
 */
static u32 mtlRegisterShader(any context, string path, string entryName,
                             u32 stage) {
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx == nullptr || path == nullptr || entryName == nullptr)
        return 0u;
    u32 count = MtlGfx_customCount(ctx);
    if (count >= MTL_MAX_CUSTOM_SHADERS)
        return 0u;
    bool ownedLib = false;
    id<MTLLibrary> lib = mtlAcquireLibrary(ctx, path, &ownedLib);
    if (lib == nil)
        return 0u;

    if (stage == 0u) {
        id<MTLRenderPipelineState> rpipe = mtlBuildRenderPipeline(
            MtlGfx_device(ctx), lib, entryName, MTLPixelFormatRGBA8Unorm_sRGB);
        id<MTLRenderPipelineState> spipe = mtlBuildRenderPipeline(
            MtlGfx_device(ctx), lib, entryName, MTLPixelFormatBGRA8Unorm_sRGB);
        if (ownedLib)
            [lib release];
        if (rpipe == nil || spipe == nil) {
            if (rpipe != nil)
                [rpipe release];
            if (spipe != nil)
                [spipe release];
            return 0u;
        }
        MtlGfx_setRenderPipeAt(ctx, count, rpipe);
        MtlGfx_setSurfaceRenderPipeAt(ctx, count, spipe);
        MtlGfx_setCustomCount(ctx, count + 1u);
        return count + 1u;
    }

    id<MTLComputePipelineState> pipe = mtlBuildPipeline(MtlGfx_device(ctx), lib, entryName);
    if (ownedLib)
        [lib release];
    if (pipe == nil)
        return 0u;
    MtlGfx_setCustomPipeAt(ctx, count, pipe);
    MtlGfx_setCustomCount(ctx, count + 1u);
    return count + 1u;
}

static bool mtlIsMetalName(string s) {
    if (s == nullptr)
        return false;
    return s[0] == 'm' && s[1] == 'e' && s[2] == 't' && s[3] == 'a' && s[4] == 'l' && s[5] == '\0';
}

static MtlGraphicsContext *MtlGraphicsContext_init(VideoPool *pool) {
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> queue = nil;
    bool ownsDevice = false;

    VideoPoolDriver poolDriver = VideoPool_driver(pool);
    if (mtlIsMetalName(poolDriver.name) && poolDriver.context != nullptr) {
        MtlPoolContext *pctx = (MtlPoolContext*) poolDriver.context;
        dev = MtlPoolContext_device(pctx);
        queue = MtlPoolContext_queue(pctx);
    } else {
        dev = MTLCreateSystemDefaultDevice();
        if (dev == nil)
            return nullptr;
        queue = [dev newCommandQueue];
        if (queue == nil) {
            [dev release];
            return nullptr;
        }
        ownsDevice = true;
    }

    id<MTLLibrary> lib = mtlLoadBuiltinLibrary(dev);
    if (lib == nil) {
        if (ownsDevice) {
            [queue release];
            [dev release];
        }
        THROW("MetalGraphicsDriver: failed to compile builtin MSL kernels");
        return nullptr;
    }

    id<MTLComputePipelineState> clearPipe = mtlBuildPipeline(dev, lib, "graphvex_clear_rgba8");
    id<MTLComputePipelineState> copyPipe = mtlBuildPipeline(dev, lib, "graphvex_copy_bytes");
    id<MTLComputePipelineState> copyToTexPipe = mtlBuildPipeline(dev, lib, "graphvex_copy_to_tex_rgba8");
    id<MTLComputePipelineState> presentPipe = mtlBuildPipeline(dev, lib, "graphvex_present_rgba8");
    if (clearPipe == nil || copyPipe == nil || copyToTexPipe == nil || presentPipe == nil) {
        if (presentPipe != nil)
            [presentPipe release];
        if (copyToTexPipe != nil)
            [copyToTexPipe release];
        if (copyPipe != nil)
            [copyPipe release];
        if (clearPipe != nil)
            [clearPipe release];
        [lib release];
        if (ownsDevice) {
            [queue release];
            [dev release];
        }
        THROW("MetalGraphicsDriver: failed to build compute pipelines");
        return nullptr;
    }

    SecureMemoryArena *sa = Graphvex_secureArena();
    SecureHandle sh = SecureMemoryArena_alloc(sa, 0u, sizeof(MtlGraphicsContext));
    MtlGraphicsContext *ctx = SecureHandle_isValid(sh)
        ? (MtlGraphicsContext*) SecureMemoryArena_resolve(sa, sh)
        : nullptr;
    if (ctx == nullptr) {
        if (SecureHandle_isValid(sh))
            (discard) SecureMemoryArena_free(sa, sh);
        [presentPipe release];
        [copyToTexPipe release];
        [copyPipe release];
        [clearPipe release];
        [lib release];
        if (ownsDevice) {
            [queue release];
            [dev release];
        }
        return nullptr;
    }
    memset(ctx, 0, sizeof *ctx);
    MtlGfx_setSelfHandle(ctx, sh);
    MtlGfx_setDevice(ctx, dev);
    MtlGfx_setQueue(ctx, queue);
    MtlGfx_setLibrary(ctx, lib);
    MtlGfx_setClearPipe(ctx, clearPipe);
    MtlGfx_setCopyPipe(ctx, copyPipe);
    MtlGfx_setCopyToTexPipe(ctx, copyToTexPipe);
    MtlGfx_setPresentPipe(ctx, presentPipe);
    MtlGfx_setOwnsDevice(ctx, ownsDevice);
    return ctx;
}

;;CONSTRUCTOR
/**
 * Creates a Metal-backed Graphics context bound to the given VideoPool.
 * @param pool backing Metal VideoPool
 * @return newly allocated Graphics context, or nullptr on failure
 */
Graphics *Graphics_metal(VideoPool *pool)
{
    if (pool == nullptr)
        return nullptr;
    MtlGraphicsContext *ctx = MtlGraphicsContext_init(pool);
    if (ctx == nullptr)
        return nullptr;
    GraphicsDriver driver = {
        .name = "metal",
        .context = ctx,
        .registerShader = mtlRegisterShader,
        .submitCommands = mtlSubmitCommands,
        .submitToSurface = mtlSubmitToSurface,
        .presentImage = mtlPresentImage,
        .setSyncPresent = mtlSetSyncPresent,
        .setSurfacePurgeable = mtlSetSurfacePurgeable,
        .destroyContext = mtlDestroyGraphicsContext,
    };
    Graphics *g = Graphics(pool, &driver);
    if (g == nullptr)
        mtlDestroyGraphicsContext(ctx);
    return g;
}
