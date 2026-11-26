#ifndef GRAPHVEX_MTL_INTERNAL_H
#define GRAPHVEX_MTL_INTERNAL_H

/**
 * metal/mtl_internal.h -- private shared Metal device/slab structs between
 * mtl_vpool.m and mtl_graphics.m. Never included outside metal/.
 */

#include "vex.h"
#include "vio/vio_internal.h"
#include "sandcastles/nio/nio.h"

#if !defined(VEX_PLATFORM_APPLE)
#error "graphvex/metal/mtl_internal.h requires VEX_PLATFORM_APPLE"
#endif

#import <Metal/MTLDevice.h>
#import <Metal/MTLBuffer.h>
#import <Metal/MTLCommandBuffer.h>
#import <Metal/MTLCommandQueue.h>
#import <Metal/MTLTexture.h>
#import <Metal/MTLLibrary.h>
#import <Metal/MTLComputePipeline.h>
#import <Metal/MTLRenderPipeline.h>
#import <IOSurface/IOSurface.h>

#define MTL_MAX_CUSTOM_SHADERS 64u
#define MTL_MAX_CACHED_LIBS    16u
#define MTL_LIB_PATH_CAP       256u
#define MTL_SWAP_COUNT         2u

typedef struct MtlPoolContext {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    SecureHandle selfHandle;
    bool uma;
} MtlPoolContext;

typedef struct MtlSlabHandle {
    id<MTLBuffer> buffer;
    any mmapBase;
    bytes mmapLen;
    SecureHandle selfHandle;
} MtlSlabHandle;

/**
 * Uniform payload for Metal RGBA8 clear compute kernel.
 *
 * u32 strideBytes - destination image row stride in bytes
 * i32 x0          - clipped region left coordinate
 * i32 y0          - clipped region top coordinate
 * i32 x1          - clipped region right coordinate
 * i32 y1          - clipped region bottom coordinate
 * u32 rgba        - packed RGBA8 clear color
 */
typedef struct MtlClearUniform {
    u32 strideBytes;
    i32 x0;
    i32 y0;
    i32 x1;
    i32 y1;
    u32 rgba;
} MtlClearUniform;

/**
 * Uniform payload for Metal strided byte copy compute kernel.
 *
 * u32 dstStrideBytes - destination row stride in bytes
 * u32 srcStrideBytes - source row stride in bytes
 * u32 bpp            - bytes per pixel
 * i32 dstX0          - destination clip left coordinate
 * i32 dstY0          - destination clip top coordinate
 * i32 srcX0          - source clip left coordinate
 * i32 srcY0          - source clip top coordinate
 * u32 copyW          - copy width in pixels
 * u32 copyH          - copy height in pixels
 */
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

/**
 * Uniform payload for Metal RGBA8 to BGRA8 drawable present kernel.
 *
 * u32 srcWidth       - source image width in pixels
 * u32 srcHeight      - source image height in pixels
 * u32 srcStrideBytes - source row stride in bytes
 * u32 dstWidth       - drawable texture width in pixels
 * u32 dstHeight      - drawable texture height in pixels
 */
typedef struct MtlPresentUniform {
    u32 srcWidth;
    u32 srcHeight;
    u32 srcStrideBytes;
    u32 dstWidth;
    u32 dstHeight;
} MtlPresentUniform;

/**
 * Internal state for the Metal GraphicsDriver context.
 *
 * id<MTLDevice> device                         - Metal GPU device
 * id<MTLCommandQueue> queue                    - Metal command queue
 * id<MTLLibrary> builtinLibrary                - compiled builtin MSL library
 * id<MTLComputePipelineState> clearPipeline    - builtin clear kernel
 * id<MTLComputePipelineState> copyPipeline     - builtin copy kernel
 * id<MTLComputePipelineState> presentPipeline  - builtin present kernel
 * id<MTLComputePipelineState> customPipelines  - registered compute pipelines
 * id<MTLRenderPipelineState> renderPipelines   - registered render pipelines
 * id<MTLTexture> renderTex                     - offscreen render target
 * u32 renderTexW                               - offscreen texture width
 * u32 renderTexH                               - offscreen texture height
 * id<MTLCommandBuffer> pendingPresent          - last async present buffer
 * id<MTLLibrary> cachedLibraries[]               - cached compiled MSL libraries
 * utf8 cachedLibPaths[][]                        - source paths for cached libraries
 * u32 cachedLibCount                             - number of cached MSL libraries
 * u32 customCount                                - registered shader count
 * bool ownsDevice                              - true if device/queue owned
 * bool deviceLost                              - latched device-lost flag
 */
typedef struct MtlGraphicsContext {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLLibrary> builtinLibrary;
    id<MTLComputePipelineState> clearPipeline;
    id<MTLComputePipelineState> copyPipeline;
    id<MTLComputePipelineState> copyToTexPipeline;
    id<MTLComputePipelineState> presentPipeline;
    id<MTLComputePipelineState> customPipelines[MTL_MAX_CUSTOM_SHADERS];
    id<MTLRenderPipelineState> renderPipelines[MTL_MAX_CUSTOM_SHADERS];
    id<MTLRenderPipelineState> surfaceRenderPipelines[MTL_MAX_CUSTOM_SHADERS];
    id<MTLTexture> renderTex;
    u32 renderTexW;
    u32 renderTexH;
    IOSurfaceRef swapSurfaces[MTL_SWAP_COUNT];
    id<MTLTexture> swapTextures[MTL_SWAP_COUNT];
    id<MTLTexture> swapSrgbViews[MTL_SWAP_COUNT];
    u32 swapW;
    u32 swapH;
    u32 swapIndex;
    id<MTLCommandBuffer> pendingPresent;
    id<MTLLibrary> cachedLibraries[MTL_MAX_CACHED_LIBS];
    utf8 cachedLibPaths[MTL_MAX_CACHED_LIBS][MTL_LIB_PATH_CAP];
    u32 cachedLibCount;
    u32 customCount;
    SecureHandle selfHandle;
    bool ownsDevice;
    bool deviceLost;
    bool syncPresent;
} MtlGraphicsContext;

;;GETTER
static inline id<MTLDevice> MtlPoolContext_device(const MtlPoolContext *c) { return (*c).device; }
;;SETTER
static inline fn MtlPoolContext_setDevice(MtlPoolContext *c, id<MTLDevice> d)
{
    (*c).device = d;
}
;;GETTER
static inline id<MTLCommandQueue> MtlPoolContext_queue(const MtlPoolContext *c) { return (*c).queue; }
;;SETTER
static inline fn MtlPoolContext_setQueue(MtlPoolContext *c, id<MTLCommandQueue> q)
{
    (*c).queue = q;
}
;;GETTER
static inline bool MtlPoolContext_uma(const MtlPoolContext *c)
{
    return (*c).uma;
}
;;SETTER
static inline fn MtlPoolContext_setUma(MtlPoolContext *c, bool v)
{
    (*c).uma = v;
}
;;GETTER
static inline SecureHandle MtlPoolContext_selfHandle(const MtlPoolContext *c)
{
    return (*c).selfHandle;
}
;;SETTER
static inline fn MtlPoolContext_setSelfHandle(MtlPoolContext *c, SecureHandle h)
{
    (*c).selfHandle = h;
}

;;GETTER
static inline id<MTLBuffer> MtlSlabHandle_buffer(const MtlSlabHandle *h) { return (*h).buffer; }
;;SETTER
static inline fn MtlSlabHandle_setBuffer(MtlSlabHandle *h, id<MTLBuffer> b)
{
    (*h).buffer = b;
}
;;GETTER
static inline any MtlSlabHandle_mmapBase(const MtlSlabHandle *h)
{
    return (*h).mmapBase;
}
;;SETTER
static inline fn MtlSlabHandle_setMmapBase(MtlSlabHandle *h, any p)
{
    (*h).mmapBase = p;
}
;;GETTER
static inline bytes MtlSlabHandle_mmapLen(const MtlSlabHandle *h)
{
    return (*h).mmapLen;
}
;;SETTER
static inline fn MtlSlabHandle_setMmapLen(MtlSlabHandle *h, bytes l)
{
    (*h).mmapLen = l;
}
;;GETTER
static inline SecureHandle MtlSlabHandle_selfHandle(const MtlSlabHandle *h)
{
    return (*h).selfHandle;
}
;;SETTER
static inline fn MtlSlabHandle_setSelfHandle(MtlSlabHandle *h, SecureHandle sh)
{
    (*h).selfHandle = sh;
}
;;GETTER
static inline any MtlVideoSlab_driverHandle(const VideoSlabInfo *s)
{
    return (*s).driverHandle;
}

#endif
