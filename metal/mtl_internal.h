#ifndef GRAPHVEX_MTL_INTERNAL_H
#define GRAPHVEX_MTL_INTERNAL_H

/**
 * metal/mtl_internal.h -- private shared Metal device/slab/backing structs
 * between mtl_vpool.m and mtl_graphics.m. Never included outside metal/.
 */

#include "vex.h"
#include "vio/vio_internal.h"
#include "core/graphics.h"
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
#include <pthread.h>

#define MTL_MAX_CUSTOM_SHADERS  64u
#define MTL_MAX_CACHED_LIBS     16u
#define MTL_LIB_PATH_CAP        256u
#define MTL_SWAP_COUNT          2u
#define MTL_MAX_BACKING_IMAGES  64u

/**
 * Private Metal device/queue context owned by ImagePool_metal.
 *
 * id<MTLDevice> device        - Metal system device
 * id<MTLCommandQueue> queue   - Metal command queue
 * SecureHandle selfHandle     - sandcastles backing handle
 * bool uma                    - true on Apple Silicon unified memory
 */
typedef struct MtlPoolContext {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    SecureHandle selfHandle;
    bool uma;
} MtlPoolContext;

/**
 * Private Metal slab wrapper pairing an MTLBuffer with its guard-paged mmap.
 *
 * id<MTLBuffer> buffer    - wrapped or private MTLBuffer
 * any mmapBase            - base address of PROT_NONE-guarded mapping
 * bytes mmapLen           - total mmap length including guard pages
 * SecureHandle selfHandle - sandcastles backing handle
 */
typedef struct MtlSlabHandle {
    id<MTLBuffer> buffer;
    any mmapBase;
    bytes mmapLen;
    SecureHandle selfHandle;
} MtlSlabHandle;

/**
 * Driver-side entry for one imported or allocated IOSurface GraphicsBackingImage.
 *
 * IOSurfaceRef surface   - retained IOSurface handle
 * id<MTLTexture> plane0  - Metal texture view for plane 0 (RGBA/BGRA or NV12 Y)
 * id<MTLTexture> plane1  - Metal texture view for plane 1 (NV12 UV, or nil)
 * u32 width              - surface width in pixels
 * u32 height             - surface height in pixels
 * u32 strideBytes        - primary plane bytes per row
 * PixelFormat format     - graphvex pixel format
 * u32 planeCount         - 1 or 2 planes
 * bool ownsSurface       - true if allocated by allocBacking
 * bool live              - true while slot is occupied
 */
typedef struct MtlBackingEntry {
    IOSurfaceRef surface;
    id<MTLTexture> plane0;
    id<MTLTexture> plane1;
    u32 width;
    u32 height;
    u32 strideBytes;
    PixelFormat format;
    u32 planeCount;
    bool ownsSurface;
    bool live;
} MtlBackingEntry;

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
 * id<MTLDevice> device                              - Metal GPU device
 * id<MTLCommandQueue> queue                         - Metal command queue
 * id<MTLLibrary> builtinLibrary                     - compiled builtin MSL library
 * id<MTLComputePipelineState> clearPipeline         - builtin clear kernel
 * id<MTLComputePipelineState> copyPipeline          - builtin buffer copy kernel
 * id<MTLComputePipelineState> copyToTexPipeline     - builtin buffer-to-tex kernel
 * id<MTLComputePipelineState> presentPipeline       - builtin buffer present kernel
 * id<MTLComputePipelineState> presentBackingRgbaPipe - backing RGBA present kernel
 * id<MTLComputePipelineState> presentBackingNv12Pipe - backing NV12 present kernel
 * id<MTLComputePipelineState> copyBackingRgbaPipe   - backing RGBA to buffer kernel
 * id<MTLComputePipelineState> copyBackingNv12Pipe   - backing NV12 to buffer kernel
 * id<MTLComputePipelineState> customPipelines[]     - registered compute pipelines
 * id<MTLRenderPipelineState> renderPipelines[]      - offscreen render pipelines
 * id<MTLRenderPipelineState> surfaceRenderPipelines[] - direct surface pipelines
 * id<MTLTexture> renderTex                          - offscreen render target
 * id<MTLTexture> dummyTex                           - 1x1 fallback binding texture
 * id<MTLBuffer> dummyBuf                            - 16B fallback binding buffer
 * u32 renderTexW / renderTexH                       - offscreen texture extent
 * IOSurfaceRef swapSurfaces[2]                      - double-buffered Surface backing
 * id<MTLTexture> swapTextures[2]                    - raw BGRA8 Metal texture views
 * id<MTLTexture> swapSrgbViews[2]                   - sRGB BGRA8 render views
 * u32 swapW / swapH / swapIndex                     - double-buffer state
 * MtlBackingEntry backings[]                        - GraphicsBackingImage slot table
 * pthread_mutex_t backingLock                       - guards concurrent backing import/release
 * const ImagePool *auxPools[]                       - attached sibling domain pools
 * u32 auxPoolCount                                  - attached sibling pool count
 * id<MTLCommandBuffer> pendingPresent               - last async present buffer
 * id<MTLLibrary> cachedLibraries[]                  - cached compiled MSL libraries
 * utf8 cachedLibPaths[][]                           - source paths for cached libraries
 * u32 cachedLibCount                                - number of cached MSL libraries
 * u32 customCount                                   - registered shader count
 * SecureHandle selfHandle                           - sandcastles backing handle
 * bool ownsDevice                                   - true if device/queue owned
 * bool deviceLost                                   - latched device-lost flag
 * bool syncPresent                                  - true for synchronous present
 * bool surfacePurgeable                             - true while double-buffer volatile
 */
typedef struct MtlGraphicsContext {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLLibrary> builtinLibrary;
    id<MTLComputePipelineState> clearPipeline;
    id<MTLComputePipelineState> copyPipeline;
    id<MTLComputePipelineState> copyToTexPipeline;
    id<MTLComputePipelineState> presentPipeline;
    id<MTLComputePipelineState> presentBackingRgbaPipe;
    id<MTLComputePipelineState> presentBackingNv12Pipe;
    id<MTLComputePipelineState> copyBackingRgbaPipe;
    id<MTLComputePipelineState> copyBackingNv12Pipe;
    id<MTLComputePipelineState> customPipelines[MTL_MAX_CUSTOM_SHADERS];
    id<MTLRenderPipelineState> renderPipelines[MTL_MAX_CUSTOM_SHADERS];
    id<MTLRenderPipelineState> surfaceRenderPipelines[MTL_MAX_CUSTOM_SHADERS];
    id<MTLTexture> renderTex;
    id<MTLTexture> dummyTex;
    id<MTLBuffer> dummyBuf;
    u32 renderTexW;
    u32 renderTexH;
    IOSurfaceRef swapSurfaces[MTL_SWAP_COUNT];
    id<MTLTexture> swapTextures[MTL_SWAP_COUNT];
    id<MTLTexture> swapSrgbViews[MTL_SWAP_COUNT];
    u32 swapW;
    u32 swapH;
    u32 swapIndex;
    MtlBackingEntry backings[MTL_MAX_BACKING_IMAGES];
    pthread_mutex_t backingLock;
    const ImagePool *auxPools[GRAPHICS_MAX_AUX_POOLS];
    u32 auxPoolCount;
    id<MTLCommandBuffer> pendingPresent;
    id<MTLLibrary> cachedLibraries[MTL_MAX_CACHED_LIBS];
    utf8 cachedLibPaths[MTL_MAX_CACHED_LIBS][MTL_LIB_PATH_CAP];
    u32 cachedLibCount;
    u32 customCount;
    SecureHandle selfHandle;
    bool ownsDevice;
    bool deviceLost;
    bool syncPresent;
    bool surfacePurgeable;
} MtlGraphicsContext;

;;GETTER
static inline id<MTLDevice> MtlPoolContext_device(const MtlPoolContext *c)
{
    return (*c).device;
}

;;SETTER
static inline fn MtlPoolContext_setDevice(MtlPoolContext *c, id<MTLDevice> d)
{
    (*c).device = d;
}

;;GETTER
static inline id<MTLCommandQueue> MtlPoolContext_queue(const MtlPoolContext *c)
{
    return (*c).queue;
}

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
static inline id<MTLBuffer> MtlSlabHandle_buffer(const MtlSlabHandle *h)
{
    return (*h).buffer;
}

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
static inline any MtlImageSlab_driverHandle(const ImageSlabInfo *s)
{
    return (*s).driverHandle;
}

#endif
