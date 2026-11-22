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
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <AppKit/NSView.h>
#import <AppKit/NSWindow.h>
#import <dispatch/dispatch.h>

#define MTL_ERR_DEVICE_REMOVED 11
#define MTL_MAX_CUSTOM_SHADERS 64u

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
 * FUNCTION REGISTRY: Graphics_createMetal.
 * CONTRACT: vendor Metal and QuartzCore types stay strictly inside this .m
 * file; native BGRA drawable conversion happens only inside presentImage. */

typedef struct MtlClearUniform {
    u32 strideBytes;
    i32 x0;
    i32 y0;
    i32 x1;
    i32 y1;
    u32 rgba;
} MtlClearUniform;

typedef struct MtlCopyUniform {
    u32 dstStrideBytes;
    u32 srcStrideBytes;
    u32 bpp;
    i32 dstX0;
    i32 dstY0;
    i32 srcX0;
    i32 srcY0;
    u32 copyW;
    u32 copyH;
} MtlCopyUniform;

typedef struct MtlPresentUniform {
    u32 srcWidth;
    u32 srcHeight;
    u32 srcStrideBytes;
    u32 dstWidth;
    u32 dstHeight;
} MtlPresentUniform;

typedef struct MtlGraphicsContext {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLLibrary> builtinLibrary;
    id<MTLComputePipelineState> clearPipeline;
    id<MTLComputePipelineState> copyPipeline;
    id<MTLComputePipelineState> presentPipeline;
    id<MTLComputePipelineState> customPipelines[MTL_MAX_CUSTOM_SHADERS];
    id<MTLRenderPipelineState> renderPipelines[MTL_MAX_CUSTOM_SHADERS];
    id<MTLTexture> renderTex;
    u32 renderTexW;
    u32 renderTexH;
    id<MTLCommandBuffer> pendingPresent;
    u32 customCount;
    bool ownsDevice;
    bool deviceLost;
} MtlGraphicsContext;

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
;;SETTER
static inline fn MtlGfx_setCustomCount(MtlGraphicsContext *c, u32 n)
{
    (*c).customCount = n;
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

static const u8 MTL_BUILTIN_EMBED[] = {
#embed "../shader/msl/builtin.metal"
    , 0
};

static id<MTLLibrary> mtlLoadBuiltinLibrary(id<MTLDevice> dev) {
    NSError *err = nil;
    ShaderBytes loaded = Shader_loadFile("shader/msl/builtin.metal");
    if (!ShaderBytes_isValid(&loaded)) {
        loaded = Shader_loadFile("vex/graphvex/api/shader/msl/builtin.metal");
    }
    const utf8 *srcText = ShaderBytes_isValid(&loaded)
        ? (const utf8*) ShaderBytes_data(&loaded)
        : (const utf8*) MTL_BUILTIN_EMBED;
    NSString *src = [NSString stringWithUTF8String:srcText];
    id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
    ShaderBytes_destroy(&loaded);
    return lib;
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
    if (tex != nil && MtlGfx_renderTexW(ctx) == width && MtlGfx_renderTexH(ctx) == height) {
        return tex;
    }
    if (tex != nil)
        [tex release];
    MTLTextureDescriptor *desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                     width:(NSUInteger) width
                                    height:(NSUInteger) height
                                 mipmapped:NO];
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> created = [MtlGfx_device(ctx) newTextureWithDescriptor:desc];
    if (created == nil) {
        MtlGfx_setRenderTex(ctx, nil, 0u, 0u);
        return nil;
    }
    MtlGfx_setRenderTex(ctx, created, width, height);
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
        if (srcBuf != nil) {
            [enc setFragmentBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:2];
        }
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
        return mtlWaitBounded(ctx, cmdBuf);
    }
}

static CAMetalLayer *mtlResolveSurfaceLayer(id<MTLDevice> dev, any nativeSurface,
                                            u32 width, u32 height, bool *outLiveResize) {
    if (outLiveResize != nullptr)
        *outLiveResize = false;
    id obj = (id) nativeSurface;
    if (obj == nil)
        return nil;
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
                [layer setAutoresizingMask:(kCALayerWidthSizable | kCALayerHeightSizable)];
                [layer setContentsGravity:kCAGravityTopLeft];
                [host addSublayer:layer];
            }
        }
        NSWindow *win = [view window];
        liveResize = [view inLiveResize] || (win != nil && [win inLiveResize]);
        NSRect bounds = [view bounds];
        CGFloat backingScale = (win != nil && [win backingScaleFactor] > 0.0)
            ? [win backingScaleFactor]
            : 1.0;
        CGFloat effectiveScale = backingScale;
        if (bounds.size.width > 0.0 && width > 0u) {
            CGFloat ratio = (CGFloat) width / bounds.size.width;
            if (ratio > 0.0)
                effectiveScale = ratio;
        }
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [layer setFrame:bounds];
        [layer setContentsScale:effectiveScale];
        [layer setContentsGravity:kCAGravityTopLeft];
        [CATransaction commit];
    } else {
        return nil;
    }
    if (layer.device != dev)
        layer.device = dev;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.presentsWithTransaction = liveResize ? YES : NO;
    layer.drawableSize = CGSizeMake((CGFloat) width, (CGFloat) height);
    if (outLiveResize != nullptr)
        *outLiveResize = liveResize;
    return layer;
}

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
        bool liveResize = false;
        CAMetalLayer *layer = mtlResolveSurfaceLayer(
            MtlGfx_device(ctx), nativeSurface,
            GraphicsImage_width(image), GraphicsImage_height(image), &liveResize);
        if (layer == nil)
            return false;
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (drawable == nil)
            return false;

        id<MTLBuffer> srcBuf = nil;
        bytes srcOff = 0u;
        if (!mtlResolveBuffer(pool, GraphicsImage_data(image), GraphicsImage_byteSize(image), &srcBuf, &srcOff))
            return false;

        id<MTLTexture> tex = drawable.texture;
        MtlPresentUniform u = {
            .srcWidth = GraphicsImage_width(image),
            .srcHeight = GraphicsImage_height(image),
            .srcStrideBytes = GraphicsImage_strideBytes(image),
            .dstWidth = (u32) tex.width,
            .dstHeight = (u32) tex.height,
        };

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

        if (liveResize) {
            if (!mtlWaitBounded(ctx, cmdBuf))
                return false;
            [drawable present];
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

;;DECONSTRUCT
static fn mtlDestroyGraphicsContext(any context) {
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx == nullptr)
        return;
    mtlDrainPendingPresent(ctx);
    u32 nCustom = MtlGfx_customCount(ctx);
    for (u32 i = 0u; i < nCustom; i++) {
        id<MTLComputePipelineState> p = MtlGfx_customPipeAt(ctx, i);
        if (p != nil)
            [p release];
        id<MTLRenderPipelineState> rp = MtlGfx_renderPipeAt(ctx, i);
        if (rp != nil)
            [rp release];
    }
    if (MtlGfx_renderTex(ctx) != nil)
        [MtlGfx_renderTex(ctx) release];
    if (MtlGfx_presentPipe(ctx) != nil)
        [MtlGfx_presentPipe(ctx) release];
    if (MtlGfx_copyPipe(ctx) != nil)
        [MtlGfx_copyPipe(ctx) release];
    if (MtlGfx_clearPipe(ctx) != nil)
        [MtlGfx_clearPipe(ctx) release];
    if (MtlGfx_library(ctx) != nil)
        [MtlGfx_library(ctx) release];
    if (MtlGfx_ownsDevice(ctx)) {
        if (MtlGfx_queue(ctx) != nil)
            [MtlGfx_queue(ctx) release];
        if (MtlGfx_device(ctx) != nil)
            [MtlGfx_device(ctx) release];
    }
    MemoryArena_free(Graphvex_arena(), ctx);
}

static id<MTLComputePipelineState> mtlBuildPipeline(id<MTLDevice> dev,
                                                    id<MTLLibrary> lib,
                                                    const utf8 *fnName) {
    NSString *name = [NSString stringWithUTF8String:fnName];
    id<MTLFunction> func = [lib newFunctionWithName:name];
    if (func == nil)
        return nil;
    NSError *err = nil;
    id<MTLComputePipelineState> pipe = [dev newComputePipelineStateWithFunction:func error:&err];
    [func release];
    return pipe;
}

static id<MTLRenderPipelineState> mtlBuildRenderPipeline(id<MTLDevice> dev,
                                                         id<MTLLibrary> lib,
                                                         const utf8 *entryPair) {
    const utf8 *colon = nullptr;
    for (const utf8 *p = entryPair; *p != '\0'; p++) {
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
        if (vertFn != nil)
            [vertFn release];
        if (fragFn != nil)
            [fragFn release];
        return nil;
    }

    MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vertFn;
    desc.fragmentFunction = fragFn;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
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
    return pipe;
}

static u32 mtlRegisterShader(any context, const utf8 *path, const utf8 *entryName,
                             u32 stage) {
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx == nullptr || path == nullptr || entryName == nullptr)
        return 0u;
    u32 count = MtlGfx_customCount(ctx);
    if (count >= MTL_MAX_CUSTOM_SHADERS)
        return 0u;
    ShaderBytes loaded = Shader_loadFile(path);
    if (!ShaderBytes_isValid(&loaded))
        return 0u;
    NSError *err = nil;
    NSString *src = [NSString stringWithUTF8String:(const utf8*) ShaderBytes_data(&loaded)];
    id<MTLLibrary> lib = [MtlGfx_device(ctx) newLibraryWithSource:src options:nil error:&err];
    ShaderBytes_destroy(&loaded);
    if (lib == nil)
        return 0u;

    if (stage == 0u) {
        id<MTLRenderPipelineState> rpipe = mtlBuildRenderPipeline(MtlGfx_device(ctx), lib, entryName);
        [lib release];
        if (rpipe == nil)
            return 0u;
        MtlGfx_setRenderPipeAt(ctx, count, rpipe);
        MtlGfx_setCustomCount(ctx, count + 1u);
        return count + 1u;
    }

    id<MTLComputePipelineState> pipe = mtlBuildPipeline(MtlGfx_device(ctx), lib, entryName);
    [lib release];
    if (pipe == nil)
        return 0u;
    MtlGfx_setCustomPipeAt(ctx, count, pipe);
    MtlGfx_setCustomCount(ctx, count + 1u);
    return count + 1u;
}

static bool mtlIsMetalName(const utf8 *s) {
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
    id<MTLComputePipelineState> presentPipe = mtlBuildPipeline(dev, lib, "graphvex_present_rgba8");
    if (clearPipe == nil || copyPipe == nil || presentPipe == nil) {
        if (presentPipe != nil)
            [presentPipe release];
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

    MtlGraphicsContext *ctx = MemoryArena_alloc(Graphvex_arena(), 0, sizeof *ctx);
    if (ctx == nullptr) {
        [presentPipe release];
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
    MtlGfx_setDevice(ctx, dev);
    MtlGfx_setQueue(ctx, queue);
    MtlGfx_setLibrary(ctx, lib);
    MtlGfx_setClearPipe(ctx, clearPipe);
    MtlGfx_setCopyPipe(ctx, copyPipe);
    MtlGfx_setPresentPipe(ctx, presentPipe);
    MtlGfx_setOwnsDevice(ctx, ownsDevice);
    return ctx;
}

Graphics *Graphics_createMetal(VideoPool *pool) {
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
        .presentImage = mtlPresentImage,
        .destroyContext = mtlDestroyGraphicsContext,
    };
    Graphics *g = Graphics_createWithDriver(pool, &driver);
    if (g == nullptr)
        mtlDestroyGraphicsContext(ctx);
    return g;
}
