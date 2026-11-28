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
#include <string.h>

#define MTL_ERR_DEVICE_REMOVED 11

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: MetalGraphicsDriver
 * ============================================================================
 * Implements the core/driver.h GraphicsDriver seam on Apple Silicon and macOS
 * Metal over vio/ ImagePool slabs and IOSurface-backed GraphicsBackingImage
 * surfaces. Clear, copy, custom compute dispatch, hardware-instanced quad draw,
 * zero-copy IOSurface sampling, and double-buffered (2-slot) Surface present
 * execute on Metal with 100 ms bounded waits and latched device-lost handling.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: MetalGraphicsDriver (metal/mtl_graphics.m)
 * ============================================================================
 * Metal GraphicsDriver over ImagePool slabs and IOSurface GraphicsBackingImage.
 *
 * --CONSTRUCTORS-------------------------
 * public   Graphics_metal(...)   creates Metal-backed Graphics context
 *
 * --FUNCTIONS----------------------------
 * private  mtlImportBacking/mtlAllocBacking/mtlReleaseBacking  IOSurface backings
 * private  mtlAttachPool/mtlDetachPool                         sibling pool table
 * private  mtlSubmitCommands/mtlSubmitToSurface/mtlPresentImage execution paths
 * private  mtlRegisterShader                                   MSL pipeline cache
 * private  mtlDestroyGraphicsContext                           teardown
 * ============================================================================
 */

;;GETTER
static inline id<MTLDevice> MtlGfx_device(const MtlGraphicsContext *c)
{
    return (*c).device;
}

;;SETTER
static inline fn MtlGfx_setDevice(MtlGraphicsContext *c, id<MTLDevice> d)
{
    (*c).device = d;
}

;;GETTER
static inline id<MTLCommandQueue> MtlGfx_queue(const MtlGraphicsContext *c)
{
    return (*c).queue;
}

;;SETTER
static inline fn MtlGfx_setQueue(MtlGraphicsContext *c, id<MTLCommandQueue> q)
{
    (*c).queue = q;
}

;;GETTER
static inline id<MTLLibrary> MtlGfx_library(const MtlGraphicsContext *c)
{
    return (*c).builtinLibrary;
}

;;SETTER
static inline fn MtlGfx_setLibrary(MtlGraphicsContext *c, id<MTLLibrary> l)
{
    (*c).builtinLibrary = l;
}

;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_clearPipe(const MtlGraphicsContext *c)
{
    return (*c).clearPipeline;
}

;;SETTER
static inline fn MtlGfx_setClearPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).clearPipeline = p;
}

;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_copyPipe(const MtlGraphicsContext *c)
{
    return (*c).copyPipeline;
}

;;SETTER
static inline fn MtlGfx_setCopyPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).copyPipeline = p;
}

;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_copyToTexPipe(const MtlGraphicsContext *c)
{
    return (*c).copyToTexPipeline;
}

;;SETTER
static inline fn MtlGfx_setCopyToTexPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).copyToTexPipeline = p;
}

;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_presentPipe(const MtlGraphicsContext *c)
{
    return (*c).presentPipeline;
}

;;SETTER
static inline fn MtlGfx_setPresentPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).presentPipeline = p;
}

;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_presentBackingRgbaPipe(const MtlGraphicsContext *c)
{
    return (*c).presentBackingRgbaPipe;
}

;;SETTER
static inline fn MtlGfx_setPresentBackingRgbaPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).presentBackingRgbaPipe = p;
}

;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_presentBackingNv12Pipe(const MtlGraphicsContext *c)
{
    return (*c).presentBackingNv12Pipe;
}

;;SETTER
static inline fn MtlGfx_setPresentBackingNv12Pipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).presentBackingNv12Pipe = p;
}

;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_copyBackingRgbaPipe(const MtlGraphicsContext *c)
{
    return (*c).copyBackingRgbaPipe;
}

;;SETTER
static inline fn MtlGfx_setCopyBackingRgbaPipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).copyBackingRgbaPipe = p;
}

;;GETTER
static inline id<MTLComputePipelineState> MtlGfx_copyBackingNv12Pipe(const MtlGraphicsContext *c)
{
    return (*c).copyBackingNv12Pipe;
}

;;SETTER
static inline fn MtlGfx_setCopyBackingNv12Pipe(MtlGraphicsContext *c, id<MTLComputePipelineState> p)
{
    (*c).copyBackingNv12Pipe = p;
}

;;GETTER
static inline id<MTLTexture> MtlGfx_dummyTex(const MtlGraphicsContext *c)
{
    return (*c).dummyTex;
}

;;SETTER
static inline fn MtlGfx_setDummyTex(MtlGraphicsContext *c, id<MTLTexture> t)
{
    (*c).dummyTex = t;
}

;;GETTER
static inline id<MTLBuffer> MtlGfx_dummyBuf(const MtlGraphicsContext *c)
{
    return (*c).dummyBuf;
}

;;SETTER
static inline fn MtlGfx_setDummyBuf(MtlGraphicsContext *c, id<MTLBuffer> b)
{
    (*c).dummyBuf = b;
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
static inline id<MTLComputePipelineState> MtlGfx_customPipeAt(const MtlGraphicsContext *c, u32 idx)
{
    return (*c).customPipelines[idx];
}

;;SETTER
static inline fn MtlGfx_setCustomPipeAt(MtlGraphicsContext *c, u32 idx, id<MTLComputePipelineState> p)
{
    (*c).customPipelines[idx] = p;
}

;;GETTER
static inline id<MTLRenderPipelineState> MtlGfx_renderPipeAt(const MtlGraphicsContext *c, u32 idx)
{
    return (*c).renderPipelines[idx];
}

;;SETTER
static inline fn MtlGfx_setRenderPipeAt(MtlGraphicsContext *c, u32 idx, id<MTLRenderPipelineState> p)
{
    (*c).renderPipelines[idx] = p;
}

;;GETTER
static inline id<MTLRenderPipelineState> MtlGfx_surfaceRenderPipeAt(const MtlGraphicsContext *c, u32 idx)
{
    return (*c).surfaceRenderPipelines[idx];
}

;;SETTER
static inline fn MtlGfx_setSurfaceRenderPipeAt(MtlGraphicsContext *c, u32 idx, id<MTLRenderPipelineState> p)
{
    (*c).surfaceRenderPipelines[idx] = p;
}

;;GETTER
static inline id<MTLTexture> MtlGfx_renderTex(const MtlGraphicsContext *c)
{
    return (*c).renderTex;
}

;;SETTER
static inline fn MtlGfx_setRenderTex(MtlGraphicsContext *c, id<MTLTexture> t, u32 w, u32 h)
{
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
static inline id<MTLCommandBuffer> MtlGfx_pendingPresent(const MtlGraphicsContext *c)
{
    return (*c).pendingPresent;
}

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

;;GETTER
static inline u32 MtlGfx_auxPoolCount(const MtlGraphicsContext *c)
{
    if (!c)
        return 0u;

    return (*c).auxPoolCount;
}

;;SETTER
static inline fn MtlGfx_setAuxPoolCount(MtlGraphicsContext *c, u32 count)
{
    if (!c)
        return;

    (*c).auxPoolCount = count;
}

;;GETTER
static inline const ImagePool *MtlGfx_auxPoolAt(const MtlGraphicsContext *c, u32 idx)
{
    if (!c || idx >= GRAPHICS_MAX_AUX_POOLS)
        return nullptr;

    return (*c).auxPools[idx];
}

;;SETTER
static inline fn MtlGfx_setAuxPoolAt(MtlGraphicsContext *c, u32 idx, const ImagePool *p)
{
    if (!c || idx >= GRAPHICS_MAX_AUX_POOLS)
        return;

    (*c).auxPools[idx] = p;
}

;;GETTER
static inline MtlBackingEntry *MtlGfx_backingMut(MtlGraphicsContext *c, u32 idx)
{
    if (!c || idx >= MTL_MAX_BACKING_IMAGES)
        return nullptr;

    return &(*c).backings[idx];
}

;;GETTER
static inline pthread_mutex_t *MtlGfx_backingLock(MtlGraphicsContext *c)
{
    if (!c)
        return nullptr;

    return &(*c).backingLock;
}

;;GETTER
static inline const MtlBackingEntry *MtlGfx_backingConst(const MtlGraphicsContext *c, u32 idx)
{
    if (!c || idx >= MTL_MAX_BACKING_IMAGES)
        return nullptr;

    return &(*c).backings[idx];
}

;;GETTER
static inline bool MtlBacking_live(const MtlBackingEntry *e)
{
    if (!e)
        return false;

    return (*e).live;
}

;;GETTER
static inline IOSurfaceRef MtlBacking_surface(const MtlBackingEntry *e)
{
    if (!e)
        return nullptr;

    return (*e).surface;
}

;;GETTER
static inline id<MTLTexture> MtlBacking_plane0(const MtlBackingEntry *e)
{
    return (*e).plane0;
}

;;GETTER
static inline id<MTLTexture> MtlBacking_plane1(const MtlBackingEntry *e)
{
    return (*e).plane1;
}

;;GETTER
static inline PixelFormat MtlBacking_format(const MtlBackingEntry *e)
{
    if (!e)
        return PIXEL_FORMAT_RGBA8_UNORM;

    return (*e).format;
}

;;SETTER
static inline fn MtlBacking_set(MtlBackingEntry *e, IOSurfaceRef surf,
                                id<MTLTexture> p0, id<MTLTexture> p1,
                                u32 w, u32 h, u32 strideBytes,
                                PixelFormat fmt, u32 planeCount, bool owns)
{
    if (!e)
        return;

    (*e).surface = surf;
    (*e).plane0 = p0;
    (*e).plane1 = p1;
    (*e).width = w;
    (*e).height = h;
    (*e).strideBytes = strideBytes;
    (*e).format = fmt;
    (*e).planeCount = planeCount;
    (*e).ownsSurface = owns;
    (*e).live = true;
}

;;SETTER
static inline fn MtlBacking_clear(MtlBackingEntry *e)
{
    if (!e)
        return;

    memset(e, 0, sizeof(MtlBackingEntry));
}

static const u8 MTL_BUILTIN_EMBED[] = {
#embed "builtin.metal"
    , 0
};

static id<MTLLibrary> mtlLoadBuiltinLibrary(id<MTLDevice> dev)
{
    NSError *err = nil;
    NSString *src = [NSString stringWithUTF8String:(string) MTL_BUILTIN_EMBED];
    return [dev newLibraryWithSource:src options:nil error:&err];
}

static bool mtlResolveBufferInPool(const ImagePool *pool, vpointer addr, bytes len,
                                   id<MTLBuffer> *outBuf, bytes *outOffset)
{
    ImageSlabInfo slab;
    memset(&slab, 0, sizeof(ImageSlabInfo));
    bytes off = 0u;
    if (!ImagePool_findByGpu(pool, addr, len, &slab, &off))
        return false;

    any rawHandle = MtlImageSlab_driverHandle(&slab);
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

static bool mtlResolveBuffer(const MtlGraphicsContext *ctx, const ImagePool *pool,
                             vpointer addr, bytes len,
                             id<MTLBuffer> *outBuf, bytes *outOffset)
{
    if (addr == 0u || len == 0u || outBuf == nullptr || outOffset == nullptr)
        return false;

    if (pool != nullptr && mtlResolveBufferInPool(pool, addr, len, outBuf, outOffset))
        return true;

    if (ctx != nullptr) {
        u32 nAux = MtlGfx_auxPoolCount(ctx);
        for (u32 i = 0u; i < nAux; i++) {
            const ImagePool *aux = MtlGfx_auxPoolAt(ctx, i);
            if (aux != nullptr && mtlResolveBufferInPool(aux, addr, len, outBuf, outOffset))
                return true;
        }
    }
    return false;
}

static bool mtlResolveBacking(MtlGraphicsContext *ctx, u32 backingSlot,
                              id<MTLTexture> *outPlane0, id<MTLTexture> *outPlane1,
                              PixelFormat *outFmt)
{
    if (!ctx || backingSlot == 0u || backingSlot > MTL_MAX_BACKING_IMAGES)
        return false;

    pthread_mutex_t *lock = MtlGfx_backingLock(ctx);
    pthread_mutex_lock(lock);
    const MtlBackingEntry *entry = MtlGfx_backingConst(ctx, backingSlot - 1u);
    if (!MtlBacking_live(entry)) {
        pthread_mutex_unlock(lock);
        return false;
    }

    id<MTLTexture> p0 = MtlBacking_plane0(entry);
    id<MTLTexture> p1 = MtlBacking_plane1(entry);
    PixelFormat fmt = MtlBacking_format(entry);
    pthread_mutex_unlock(lock);

    if (p0 == nil)
        return false;

    if (outPlane0)
        *outPlane0 = p0;

    if (outPlane1)
        *outPlane1 = p1;

    if (outFmt)
        *outFmt = fmt;

    return true;
}

static bool mtlImportBacking(any context, any nativeBacking,
                             u32 width, u32 height, PixelFormat format,
                             bool ownsSurface,
                             u32 *outBackingSlot, u32 *outPlaneCount,
                             u32 *outStrideBytes)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (!ctx || !nativeBacking || width == 0u || height == 0u || !outBackingSlot)
        return false;

    IOSurfaceRef surf = (IOSurfaceRef) nativeBacking;
    id<MTLDevice> dev = MtlGfx_device(ctx);
    id<MTLTexture> p0 = nil;
    id<MTLTexture> p1 = nil;
    u32 planeCount = 1u;
    u32 strideBytes = 0u;

    if (format == PIXEL_FORMAT_NV12) {
        if (IOSurfaceGetPlaneCount(surf) < 2u)
            return false;

        u32 uvW = (width + 1u) / 2u;
        u32 uvH = (height + 1u) / 2u;
        MTLTextureDescriptor *yDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                         width:(NSUInteger) width
                                        height:(NSUInteger) height
                                     mipmapped:NO];
        yDesc.storageMode = MTLStorageModeShared;
        yDesc.usage = MTLTextureUsageShaderRead;
        p0 = [dev newTextureWithDescriptor:yDesc iosurface:surf plane:0u];

        MTLTextureDescriptor *uvDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm
                                         width:(NSUInteger) uvW
                                        height:(NSUInteger) uvH
                                     mipmapped:NO];
        uvDesc.storageMode = MTLStorageModeShared;
        uvDesc.usage = MTLTextureUsageShaderRead;
        p1 = [dev newTextureWithDescriptor:uvDesc iosurface:surf plane:1u];

        if (p0 == nil || p1 == nil) {
            if (p0 != nil)
                [p0 release];
            if (p1 != nil)
                [p1 release];
            return false;
        }
        planeCount = 2u;
        strideBytes = (u32) IOSurfaceGetBytesPerRowOfPlane(surf, 0u);
    } else {
        u32 pf = (u32) IOSurfaceGetPixelFormat(surf);
        MTLPixelFormat mtlFmt = (pf == 0x42475241u)
            ? MTLPixelFormatBGRA8Unorm
            : MTLPixelFormatRGBA8Unorm;
        MTLTextureDescriptor *desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:mtlFmt
                                         width:(NSUInteger) width
                                        height:(NSUInteger) height
                                     mipmapped:NO];
        desc.storageMode = MTLStorageModeShared;
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
        p0 = [dev newTextureWithDescriptor:desc iosurface:surf plane:0u];
        if (p0 == nil)
            return false;

        planeCount = 1u;
        strideBytes = (u32) IOSurfaceGetBytesPerRow(surf);
    }

    if (strideBytes == 0u)
        strideBytes = width * PixelFormat_bytesPerPixel(format);

    pthread_mutex_t *lock = MtlGfx_backingLock(ctx);
    pthread_mutex_lock(lock);
    u32 freeIdx = MTL_MAX_BACKING_IMAGES;
    for (u32 i = 0u; i < MTL_MAX_BACKING_IMAGES; i++) {
        if (!MtlBacking_live(MtlGfx_backingConst(ctx, i))) {
            freeIdx = i;
            break;
        }
    }
    if (freeIdx >= MTL_MAX_BACKING_IMAGES) {
        pthread_mutex_unlock(lock);
        if (p1 != nil)
            [p1 release];
        if (p0 != nil)
            [p0 release];
        return false;
    }

    if (!ownsSurface)
        CFRetain(surf);

    MtlBackingEntry *entry = MtlGfx_backingMut(ctx, freeIdx);
    MtlBacking_set(entry, surf, p0, p1, width, height, strideBytes, format, planeCount, ownsSurface);
    pthread_mutex_unlock(lock);

    *outBackingSlot = freeIdx + 1u;
    if (outPlaneCount)
        *outPlaneCount = planeCount;

    if (outStrideBytes)
        *outStrideBytes = strideBytes;

    return true;
}

static bool mtlAllocBacking(any context, u32 width, u32 height, PixelFormat format,
                            any *outNativeBacking, u32 *outBackingSlot,
                            u32 *outPlaneCount, u32 *outStrideBytes)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (!ctx || width == 0u || height == 0u || !outBackingSlot)
        return false;

    @autoreleasepool {
        IOSurfaceRef surf = nullptr;
        if (format == PIXEL_FORMAT_NV12) {
            u32 yStride = (width + 63u) & ~63u;
            u32 uvW = (width + 1u) / 2u;
            u32 uvH = (height + 1u) / 2u;
            u32 uvStride = ((uvW * 2u) + 63u) & ~63u;
            u32 ySize = yStride * height;
            u32 uvSize = uvStride * uvH;
            NSArray *planes = @[
                @{
                    (id) kIOSurfacePlaneWidth: @(width),
                    (id) kIOSurfacePlaneHeight: @(height),
                    (id) kIOSurfacePlaneBytesPerElement: @(1u),
                    (id) kIOSurfacePlaneBytesPerRow: @(yStride),
                    (id) kIOSurfacePlaneOffset: @(0u),
                    (id) kIOSurfacePlaneSize: @(ySize),
                },
                @{
                    (id) kIOSurfacePlaneWidth: @(uvW),
                    (id) kIOSurfacePlaneHeight: @(uvH),
                    (id) kIOSurfacePlaneBytesPerElement: @(2u),
                    (id) kIOSurfacePlaneBytesPerRow: @(uvStride),
                    (id) kIOSurfacePlaneOffset: @(ySize),
                    (id) kIOSurfacePlaneSize: @(uvSize),
                },
            ];
            NSDictionary *props = @{
                (id) kIOSurfaceWidth: @(width),
                (id) kIOSurfaceHeight: @(height),
                (id) kIOSurfacePixelFormat: @(0x34323076u),
                (id) kIOSurfacePlaneInfo: planes,
                (id) kIOSurfaceAllocSize: @(ySize + uvSize),
            };
            surf = IOSurfaceCreate((CFDictionaryRef) props);
        } else {
            u32 rowBytes = ((width * 4u) + 63u) & ~63u;
            NSDictionary *props = @{
                (id) kIOSurfaceWidth: @(width),
                (id) kIOSurfaceHeight: @(height),
                (id) kIOSurfaceBytesPerElement: @(4u),
                (id) kIOSurfaceBytesPerRow: @(rowBytes),
                (id) kIOSurfacePixelFormat: @(0x52474241u),
            };
            surf = IOSurfaceCreate((CFDictionaryRef) props);
        }

        if (surf == nullptr)
            return false;

        if (!mtlImportBacking(context, (any) surf, width, height, format, true,
                              outBackingSlot, outPlaneCount, outStrideBytes)) {
            CFRelease(surf);
            return false;
        }
        if (outNativeBacking)
            *outNativeBacking = (any) surf;

        return true;
    }
}

static fn mtlReleaseBacking(any context, u32 backingSlot)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (!ctx || backingSlot == 0u || backingSlot > MTL_MAX_BACKING_IMAGES)
        return;

    pthread_mutex_t *lock = MtlGfx_backingLock(ctx);
    pthread_mutex_lock(lock);
    MtlBackingEntry *entry = MtlGfx_backingMut(ctx, backingSlot - 1u);
    if (!MtlBacking_live(entry)) {
        pthread_mutex_unlock(lock);
        return;
    }

    id<MTLTexture> p1 = MtlBacking_plane1(entry);
    id<MTLTexture> p0 = MtlBacking_plane0(entry);
    IOSurfaceRef surf = MtlBacking_surface(entry);
    MtlBacking_clear(entry);
    pthread_mutex_unlock(lock);

    if (p1 != nil)
        [p1 release];

    if (p0 != nil)
        [p0 release];

    if (surf != nullptr)
        CFRelease(surf);
}

static bool mtlSetBackingPurgeable(any context, u32 backingSlot, bool purgeable)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (!ctx || backingSlot == 0u || backingSlot > MTL_MAX_BACKING_IMAGES)
        return false;

    pthread_mutex_t *lock = MtlGfx_backingLock(ctx);
    pthread_mutex_lock(lock);
    const MtlBackingEntry *entry = MtlGfx_backingConst(ctx, backingSlot - 1u);
    if (!MtlBacking_live(entry)) {
        pthread_mutex_unlock(lock);
        return false;
    }

    IOSurfaceRef surf = MtlBacking_surface(entry);
    if (surf == nullptr) {
        pthread_mutex_unlock(lock);
        return false;
    }

    u32 state = purgeable ? kIOSurfacePurgeableVolatile : kIOSurfacePurgeableNonVolatile;
    bool ok = IOSurfaceSetPurgeable(surf, state, nullptr) == kIOReturnSuccess;
    pthread_mutex_unlock(lock);
    return ok;
}

static bool mtlIsBackingInUse(any context, u32 backingSlot)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (!ctx || backingSlot == 0u || backingSlot > MTL_MAX_BACKING_IMAGES)
        return false;

    pthread_mutex_t *lock = MtlGfx_backingLock(ctx);
    pthread_mutex_lock(lock);
    const MtlBackingEntry *entry = MtlGfx_backingConst(ctx, backingSlot - 1u);
    if (!MtlBacking_live(entry)) {
        pthread_mutex_unlock(lock);
        return false;
    }

    IOSurfaceRef surf = MtlBacking_surface(entry);
    bool inUse = (surf != nullptr && IOSurfaceIsInUse(surf)) ? true : false;
    pthread_mutex_unlock(lock);
    return inUse;
}

static bool mtlAttachPool(any context, const ImagePool *auxPool)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (!ctx || !auxPool)
        return false;

    u32 count = MtlGfx_auxPoolCount(ctx);
    for (u32 i = 0u; i < count; i++) {
        if (MtlGfx_auxPoolAt(ctx, i) == auxPool)
            return true;
    }
    if (count >= GRAPHICS_MAX_AUX_POOLS)
        return false;

    MtlGfx_setAuxPoolAt(ctx, count, auxPool);
    MtlGfx_setAuxPoolCount(ctx, count + 1u);
    return true;
}

static fn mtlDetachPool(any context, const ImagePool *auxPool)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (!ctx || !auxPool)
        return;

    u32 count = MtlGfx_auxPoolCount(ctx);
    for (u32 i = 0u; i < count; i++) {
        if (MtlGfx_auxPoolAt(ctx, i) == auxPool) {
            for (u32 j = i; j + 1u < count; j++)
                MtlGfx_setAuxPoolAt(ctx, j, MtlGfx_auxPoolAt(ctx, j + 1u));

            MtlGfx_setAuxPoolAt(ctx, count - 1u, nullptr);
            MtlGfx_setAuxPoolCount(ctx, count - 1u);
            return;
        }
    }
}

static bool mtlWaitBounded(MtlGraphicsContext *ctx, id<MTLCommandBuffer> cmd)
{
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
                        u32 width, u32 height)
{
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

static id<MTLTexture> mtlEnsureRenderTexture(MtlGraphicsContext *ctx, u32 width, u32 height)
{
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

static bool mtlEncodeClear(MtlGraphicsContext *ctx, const ImagePool *pool,
                           id<MTLCommandBuffer> cmdBuf, GraphicsCommand cmd)
{
    Image dst = cmd.target;
    if (!Image_isGraphics(&dst) || !Image_isValid(&dst) || cmd.targetData == 0u)
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
    if (!mtlResolveBuffer(ctx, pool, cmd.targetData, Image_byteSize(&dst), &dstBuf, &dstOff))
        return false;

    MtlClearUniform u;
    memset(&u, 0, sizeof(MtlClearUniform));
    u.strideBytes = dst.strideBytes;
    u.x0 = clip.x0;
    u.y0 = clip.y0;
    u.x1 = clip.x1;
    u.y1 = clip.y1;
    u.rgba = cmd.clearColorRGBA;

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

static bool mtlEncodeCopy(MtlGraphicsContext *ctx, const ImagePool *pool,
                          id<MTLCommandBuffer> cmdBuf, GraphicsCommand cmd)
{
    Image dst = cmd.target;
    Image src = cmd.source;
    if (!Image_isGraphics(&dst) || !Image_isValid(&dst) || cmd.targetData == 0u)
        return false;

    if (!Image_isValid(&src))
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
    if (!mtlResolveBuffer(ctx, pool, cmd.targetData, Image_byteSize(&dst), &dstBuf, &dstOff))
        return false;

    MtlCopyUniform u;
    memset(&u, 0, sizeof(MtlCopyUniform));
    u.dstStrideBytes = dst.strideBytes;
    u.srcStrideBytes = src.strideBytes;
    u.bpp = bpp;
    u.dstX0 = clip.x0;
    u.dstY0 = clip.y0;
    u.srcX0 = clip.x0 - cmd.dstX;
    u.srcY0 = clip.y0 - cmd.dstY;
    u.copyW = GraphicsRect_width(clip);
    u.copyH = GraphicsRect_height(clip);

    if (Image_isBacking(&src)) {
        id<MTLTexture> p0 = nil;
        id<MTLTexture> p1 = nil;
        PixelFormat bFmt = PIXEL_FORMAT_RGBA8_UNORM;
        if (!mtlResolveBacking(ctx, cmd.sourceBackingSlot, &p0, &p1, &bFmt))
            return false;

        id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
        if (enc == nil)
            return false;

        if (bFmt == PIXEL_FORMAT_NV12 && p1 != nil) {
            id<MTLComputePipelineState> pipe = MtlGfx_copyBackingNv12Pipe(ctx);
            [enc setComputePipelineState:pipe];
            [enc setBuffer:dstBuf offset:(NSUInteger) dstOff atIndex:0];
            [enc setTexture:p0 atIndex:0];
            [enc setTexture:p1 atIndex:1];
            [enc setBytes:&u length:sizeof u atIndex:1];
            mtlDispatch2D(enc, pipe, u.copyW, u.copyH);
        } else {
            id<MTLComputePipelineState> pipe = MtlGfx_copyBackingRgbaPipe(ctx);
            [enc setComputePipelineState:pipe];
            [enc setBuffer:dstBuf offset:(NSUInteger) dstOff atIndex:0];
            [enc setTexture:p0 atIndex:0];
            [enc setBytes:&u length:sizeof u atIndex:1];
            mtlDispatch2D(enc, pipe, u.copyW, u.copyH);
        }
        [enc endEncoding];
        return true;
    }

    id<MTLBuffer> srcBuf = nil;
    bytes srcOff = 0u;
    if (!mtlResolveBuffer(ctx, pool, cmd.sourceData, Image_byteSize(&src), &srcBuf, &srcOff))
        return false;

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

static bool mtlEncodeDispatch(MtlGraphicsContext *ctx, const ImagePool *pool,
                              id<MTLCommandBuffer> cmdBuf, GraphicsCommand cmd)
{
    if (cmd.shaderId == 0u || cmd.shaderId > MtlGfx_customCount(ctx))
        return false;

    id<MTLComputePipelineState> pipe = MtlGfx_customPipeAt(ctx, cmd.shaderId - 1u);
    if (pipe == nil)
        return false;

    id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
    if (enc == nil)
        return false;

    [enc setComputePipelineState:pipe];

    if (Image_isGraphics(&cmd.target) && Image_isValid(&cmd.target)) {
        id<MTLBuffer> dstBuf = nil;
        bytes dstOff = 0u;
        if (!mtlResolveBuffer(ctx, pool, cmd.targetData, Image_byteSize(&cmd.target), &dstBuf, &dstOff)) {
            [enc endEncoding];
            return false;
        }
        [enc setBuffer:dstBuf offset:(NSUInteger) dstOff atIndex:0];
    }
    if (Image_isGraphics(&cmd.source) && Image_isValid(&cmd.source)) {
        id<MTLBuffer> srcBuf = nil;
        bytes srcOff = 0u;
        if (!mtlResolveBuffer(ctx, pool, cmd.sourceData, Image_byteSize(&cmd.source), &srcBuf, &srcOff)) {
            [enc endEncoding];
            return false;
        }
        [enc setBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:1];
    } else if (Image_isBacking(&cmd.source) && Image_isValid(&cmd.source)) {
        id<MTLTexture> p0 = nil;
        id<MTLTexture> p1 = nil;
        PixelFormat bFmt = PIXEL_FORMAT_RGBA8_UNORM;
        if (!mtlResolveBacking(ctx, cmd.sourceBackingSlot, &p0, &p1, &bFmt)) {
            [enc endEncoding];
            return false;
        }
        [enc setTexture:p0 atIndex:0];
        [enc setTexture:(p1 != nil ? p1 : MtlGfx_dummyTex(ctx)) atIndex:1];
    }
    if (cmd.uniformAddr != 0u && cmd.uniformBytes > 0u) {
        id<MTLBuffer> uBuf = nil;
        bytes uOff = 0u;
        if (!mtlResolveBuffer(ctx, pool, cmd.uniformAddr, cmd.uniformBytes, &uBuf, &uOff)) {
            [enc endEncoding];
            return false;
        }
        [enc setBuffer:uBuf offset:(NSUInteger) uOff atIndex:2];
    }
    mtlDispatch2D(enc, pipe, cmd.gridX, cmd.gridY);
    [enc endEncoding];
    return true;
}

static fn mtlDrainPendingPresent(MtlGraphicsContext *ctx)
{
    if (ctx == nullptr)
        return;

    id<MTLCommandBuffer> pending = MtlGfx_pendingPresent(ctx);
    if (pending == nil)
        return;

    [pending waitUntilCompleted];
    [pending release];
    MtlGfx_setPendingPresent(ctx, nil);
}

static bool mtlSameTarget(GraphicsCommand a, GraphicsCommand b)
{
    return a.targetData == b.targetData &&
           a.target.width == b.target.width &&
           a.target.height == b.target.height &&
           a.target.strideBytes == b.target.strideBytes;
}

static bool mtlBindDrawSource(MtlGraphicsContext *ctx, const ImagePool *pool,
                              id<MTLRenderCommandEncoder> enc, GraphicsCommand cmd)
{
    if (!Image_isValid(&cmd.source))
        return true;

    if (Image_isGraphics(&cmd.source)) {
        id<MTLBuffer> srcBuf = nil;
        bytes srcOff = 0u;
        if (!mtlResolveBuffer(ctx, pool, cmd.sourceData, Image_byteSize(&cmd.source), &srcBuf, &srcOff))
            return false;

        [enc setFragmentBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:2];
        id<MTLTexture> dummy = MtlGfx_dummyTex(ctx);
        if (dummy != nil) {
            [enc setFragmentTexture:dummy atIndex:0];
            [enc setFragmentTexture:dummy atIndex:1];
        }
        return true;
    }

    if (Image_isBacking(&cmd.source)) {
        id<MTLTexture> p0 = nil;
        id<MTLTexture> p1 = nil;
        PixelFormat bFmt = PIXEL_FORMAT_RGBA8_UNORM;
        if (!mtlResolveBacking(ctx, cmd.sourceBackingSlot, &p0, &p1, &bFmt))
            return false;

        id<MTLBuffer> dummyB = MtlGfx_dummyBuf(ctx);
        if (dummyB != nil)
            [enc setFragmentBuffer:dummyB offset:0u atIndex:2];

        [enc setFragmentTexture:p0 atIndex:0];
        [enc setFragmentTexture:(p1 != nil ? p1 : MtlGfx_dummyTex(ctx)) atIndex:1];
        return true;
    }
    return false;
}

static bool mtlEncodeDrawRun(MtlGraphicsContext *ctx, const ImagePool *pool,
                             id<MTLCommandBuffer> cmdBuf,
                             const GraphicsCommand *cmds, u32 count,
                             u32 startIdx, u32 *outEndIdx)
{
    GraphicsCommand first = cmds[startIdx];
    Image dst = first.target;
    if (!Image_isGraphics(&dst) || !Image_isValid(&dst) || first.targetData == 0u)
        return false;

    u32 runEnd = startIdx + 1u;
    while (runEnd < count && cmds[runEnd].kind == GRAPHICS_CMD_DRAW &&
           mtlSameTarget(first, cmds[runEnd])) {
        runEnd++;
    }

    id<MTLBuffer> dstBuf = nil;
    bytes dstOff = 0u;
    if (!mtlResolveBuffer(ctx, pool, first.targetData, Image_byteSize(&dst), &dstBuf, &dstOff))
        return false;

    id<MTLTexture> tex = mtlEnsureRenderTexture(ctx, dst.width, dst.height);
    if (tex == nil)
        return false;

    MTLSize texSize = MTLSizeMake((NSUInteger) dst.width, (NSUInteger) dst.height, 1u);
    MTLOrigin zeroOrigin = MTLOriginMake(0u, 0u, 0u);
    NSUInteger imgBytes = (NSUInteger) Image_byteSize(&dst);

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

    MTLViewport vp;
    vp.originX = 0.0;
    vp.originY = 0.0;
    vp.width = (d64) dst.width;
    vp.height = (d64) dst.height;
    vp.znear = 0.0;
    vp.zfar = 1.0;
    [enc setViewport:vp];

    GraphicsRect bounds = GraphicsRect_4(0, 0, (i32) dst.width, (i32) dst.height);
    MTLScissorRect fullScissor;
    fullScissor.x = 0u;
    fullScissor.y = 0u;
    fullScissor.width = (NSUInteger) dst.width;
    fullScissor.height = (NSUInteger) dst.height;
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
        if (!mtlResolveBuffer(ctx, pool, cmd.vertexAddr, 1u, &instBuf, &instOff)) {
            [enc endEncoding];
            return false;
        }
        id<MTLBuffer> uBuf = nil;
        bytes uOff = 0u;
        if (cmd.uniformAddr != 0u && cmd.uniformBytes > 0u) {
            if (!mtlResolveBuffer(ctx, pool, cmd.uniformAddr, cmd.uniformBytes, &uBuf, &uOff)) {
                [enc endEncoding];
                return false;
            }
        }
        if (!GraphicsRect_isEmpty(cmd.scissor)) {
            GraphicsRect clip = GraphicsRect_intersect(bounds, cmd.scissor);
            if (GraphicsRect_isEmpty(clip))
                continue;

            MTLScissorRect sr;
            sr.x = (NSUInteger) clip.x0;
            sr.y = (NSUInteger) clip.y0;
            sr.width = (NSUInteger) GraphicsRect_width(clip);
            sr.height = (NSUInteger) GraphicsRect_height(clip);
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
        if (!mtlBindDrawSource(ctx, pool, enc, cmd)) {
            [enc endEncoding];
            return false;
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

static bool mtlSubmitCommands(any context, const ImagePool *pool,
                              const GraphicsCommand *cmds, u32 count)
{
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

static d64 mtlSrgbByteToLinear(u8 v)
{
    f32 c = (f32) v / 255.0f;
    f32 lin = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    return (d64) lin;
}

static MTLClearColor mtlClearColorFromRGBA(u32 rgba)
{
    return MTLClearColorMake(
        mtlSrgbByteToLinear(ColorRGBA_red(rgba)),
        mtlSrgbByteToLinear(ColorRGBA_green(rgba)),
        mtlSrgbByteToLinear(ColorRGBA_blue(rgba)),
        (d64) ColorRGBA_alpha(rgba) / 255.0);
}

static CAMetalLayer *mtlResolveSurfaceLayer(id<MTLDevice> dev, any nativeSurface,
                                            u32 viewportW, u32 viewportH,
                                            u32 surfaceW, u32 surfaceH,
                                            bool forceSync, bool *outUseSync)
{
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

    if (layer.maximumDrawableCount != MTL_SWAP_COUNT)
        layer.maximumDrawableCount = MTL_SWAP_COUNT;

    layer.presentsWithTransaction = useSync ? YES : NO;
    CGSize curSize = layer.drawableSize;
    if (curSize.width != (CGFloat) drawW || curSize.height != (CGFloat) drawH)
        layer.drawableSize = CGSizeMake((CGFloat) drawW, (CGFloat) drawH);

    if (outUseSync != nullptr)
        *outUseSync = useSync;

    return layer;
}

static bool mtlFlushSurfaceClear(id<MTLCommandBuffer> cmdBuf,
                                 id<MTLTexture> srgbView, u32 clearRGBA)
{
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

static bool mtlEncodeSurfaceCopy(MtlGraphicsContext *ctx, const ImagePool *pool,
                                 id<MTLCommandBuffer> cmdBuf, id<MTLTexture> rawTex,
                                 u32 viewportW, u32 viewportH, GraphicsCommand cmd)
{
    Image src = cmd.source;
    if (!Image_isValid(&src))
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

    if (Image_isBacking(&src)) {
        id<MTLTexture> p0 = nil;
        id<MTLTexture> p1 = nil;
        PixelFormat bFmt = PIXEL_FORMAT_RGBA8_UNORM;
        if (!mtlResolveBacking(ctx, cmd.sourceBackingSlot, &p0, &p1, &bFmt))
            return false;

        MtlPresentUniform pu;
        memset(&pu, 0, sizeof(MtlPresentUniform));
        pu.srcWidth = src.width;
        pu.srcHeight = src.height;
        pu.srcStrideBytes = src.strideBytes;
        pu.dstWidth = viewportW;
        pu.dstHeight = viewportH;

        id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
        if (enc == nil)
            return false;

        if (bFmt == PIXEL_FORMAT_NV12 && p1 != nil) {
            id<MTLComputePipelineState> pipe = MtlGfx_presentBackingNv12Pipe(ctx);
            [enc setComputePipelineState:pipe];
            [enc setTexture:p0 atIndex:0];
            [enc setTexture:p1 atIndex:1];
            [enc setTexture:rawTex atIndex:2];
            [enc setBytes:&pu length:sizeof pu atIndex:0];
            mtlDispatch2D(enc, pipe, pu.dstWidth, pu.dstHeight);
        } else {
            id<MTLComputePipelineState> pipe = MtlGfx_presentBackingRgbaPipe(ctx);
            [enc setComputePipelineState:pipe];
            [enc setTexture:p0 atIndex:0];
            [enc setTexture:rawTex atIndex:1];
            [enc setBytes:&pu length:sizeof pu atIndex:0];
            mtlDispatch2D(enc, pipe, pu.dstWidth, pu.dstHeight);
        }
        [enc endEncoding];
        return true;
    }

    if (PixelFormat_bytesPerPixel(src.format) != 4u)
        return false;

    id<MTLBuffer> srcBuf = nil;
    bytes srcOff = 0u;
    if (!mtlResolveBuffer(ctx, pool, cmd.sourceData, Image_byteSize(&src), &srcBuf, &srcOff))
        return false;

    MtlCopyUniform u;
    memset(&u, 0, sizeof(MtlCopyUniform));
    u.dstStrideBytes = 0u;
    u.srcStrideBytes = src.strideBytes;
    u.bpp = 4u;
    u.dstX0 = clip.x0;
    u.dstY0 = clip.y0;
    u.srcX0 = clip.x0 - cmd.dstX;
    u.srcY0 = clip.y0 - cmd.dstY;
    u.copyW = GraphicsRect_width(clip);
    u.copyH = GraphicsRect_height(clip);

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

static bool mtlEncodeSurfaceDrawRun(MtlGraphicsContext *ctx, const ImagePool *pool,
                                    id<MTLCommandBuffer> cmdBuf, id<MTLTexture> srgbView,
                                    u32 viewportW, u32 viewportH,
                                    bool hasClear, u32 clearRGBA,
                                    const GraphicsCommand *cmds, u32 count,
                                    u32 startIdx, u32 *outEndIdx)
{
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

    MTLViewport vp;
    vp.originX = 0.0;
    vp.originY = 0.0;
    vp.width = (d64) viewportW;
    vp.height = (d64) viewportH;
    vp.znear = 0.0;
    vp.zfar = 1.0;
    [enc setViewport:vp];

    GraphicsRect bounds = GraphicsRect_4(0, 0, (i32) viewportW, (i32) viewportH);
    MTLScissorRect fullScissor;
    fullScissor.x = 0u;
    fullScissor.y = 0u;
    fullScissor.width = (NSUInteger) viewportW;
    fullScissor.height = (NSUInteger) viewportH;
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
        if (!mtlResolveBuffer(ctx, pool, cmd.vertexAddr, 1u, &instBuf, &instOff)) {
            [enc endEncoding];
            return false;
        }
        id<MTLBuffer> uBuf = nil;
        bytes uOff = 0u;
        if (cmd.uniformAddr != 0u && cmd.uniformBytes > 0u) {
            if (!mtlResolveBuffer(ctx, pool, cmd.uniformAddr, cmd.uniformBytes, &uBuf, &uOff)) {
                [enc endEncoding];
                return false;
            }
        }
        if (!GraphicsRect_isEmpty(cmd.scissor)) {
            GraphicsRect clip = GraphicsRect_intersect(bounds, cmd.scissor);
            if (GraphicsRect_isEmpty(clip))
                continue;

            MTLScissorRect sr;
            sr.x = (NSUInteger) clip.x0;
            sr.y = (NSUInteger) clip.y0;
            sr.width = (NSUInteger) GraphicsRect_width(clip);
            sr.height = (NSUInteger) GraphicsRect_height(clip);
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
        if (!mtlBindDrawSource(ctx, pool, enc, cmd)) {
            [enc endEncoding];
            return false;
        }
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0u
                vertexCount:4u
              instanceCount:(NSUInteger) cmd.drawCount];
    }
    [enc endEncoding];
    *outEndIdx = runEnd;
    return true;
}

static fn mtlReleaseSwapchain(MtlGraphicsContext *ctx)
{
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

static bool mtlEnsureSwapchain(MtlGraphicsContext *ctx, u32 width, u32 height)
{
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

static fn mtlSetSurfacePurgeable(any context, bool purgeable);

static bool mtlSubmitToSurface(any context, const ImagePool *pool,
                               any nativeSurface,
                               u32 viewportW, u32 viewportH,
                               u32 surfaceW, u32 surfaceH,
                               const GraphicsCommand *cmds, u32 count)
{
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

static bool mtlSurfacePrefersMetalLayer(any nativeSurface)
{
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

static bool mtlEncodePresentSource(MtlGraphicsContext *ctx, const ImagePool *pool,
                                   id<MTLCommandBuffer> cmdBuf, id<MTLTexture> dstTex,
                                   u32 dstW, u32 dstH, const Image *image)
{
    MtlPresentUniform u;
    memset(&u, 0, sizeof(MtlPresentUniform));
    u.srcWidth = Image_width(image);
    u.srcHeight = Image_height(image);
    u.srcStrideBytes = Image_strideBytes(image);
    u.dstWidth = dstW;
    u.dstHeight = dstH;

    if (Image_isBacking(image)) {
        const GraphicsBackingImage *bi = Image_asBackingConst(image);
        id<MTLTexture> p0 = nil;
        id<MTLTexture> p1 = nil;
        PixelFormat bFmt = PIXEL_FORMAT_RGBA8_UNORM;
        if (!mtlResolveBacking(ctx, GraphicsBackingImage_backingSlot(bi), &p0, &p1, &bFmt))
            return false;

        id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
        if (enc == nil)
            return false;

        if (bFmt == PIXEL_FORMAT_NV12 && p1 != nil) {
            id<MTLComputePipelineState> pipe = MtlGfx_presentBackingNv12Pipe(ctx);
            [enc setComputePipelineState:pipe];
            [enc setTexture:p0 atIndex:0];
            [enc setTexture:p1 atIndex:1];
            [enc setTexture:dstTex atIndex:2];
            [enc setBytes:&u length:sizeof u atIndex:0];
            mtlDispatch2D(enc, pipe, u.dstWidth, u.dstHeight);
        } else {
            id<MTLComputePipelineState> pipe = MtlGfx_presentBackingRgbaPipe(ctx);
            [enc setComputePipelineState:pipe];
            [enc setTexture:p0 atIndex:0];
            [enc setTexture:dstTex atIndex:1];
            [enc setBytes:&u length:sizeof u atIndex:0];
            mtlDispatch2D(enc, pipe, u.dstWidth, u.dstHeight);
        }
        [enc endEncoding];
        return true;
    }

    const GraphicsImage *gi = Image_asGraphicsConst(image);
    id<MTLBuffer> srcBuf = nil;
    bytes srcOff = 0u;
    if (!mtlResolveBuffer(ctx, pool, GraphicsImage_data(gi), Image_byteSize(image), &srcBuf, &srcOff))
        return false;

    id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
    if (enc == nil)
        return false;

    id<MTLComputePipelineState> pipe = MtlGfx_presentPipe(ctx);
    [enc setComputePipelineState:pipe];
    [enc setBuffer:srcBuf offset:(NSUInteger) srcOff atIndex:0];
    [enc setTexture:dstTex atIndex:0];
    [enc setBytes:&u length:sizeof u atIndex:1];
    mtlDispatch2D(enc, pipe, u.dstWidth, u.dstHeight);
    [enc endEncoding];
    return true;
}

;;INTENTION("NSView targets share the double-buffered 2-IOSurface CALayer backing with mtlSubmitToSurface so switching between direct and offscreen-filtered frames never leaves a stale CAMetalLayer occluding the window")
static bool mtlPresentImage(any context, const ImagePool *pool,
                            any nativeSurface, const Image *image)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx == nullptr || pool == nullptr || !Image_isValid(image))
        return false;

    if (!MtlGuard_check("mtlPresentImage", MtlGfx_device(ctx), MtlGfx_queue(ctx), MtlGfx_deviceLostPtr(ctx)))
        return false;

    if (nativeSurface == nullptr)
        return true;

    @autoreleasepool {
        u32 imgW = Image_width(image);
        u32 imgH = Image_height(image);

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

            id<MTLCommandBuffer> cmdBuf = [MtlGfx_queue(ctx) commandBuffer];
            if (cmdBuf == nil)
                return false;

            if (!mtlEncodePresentSource(ctx, pool, cmdBuf, rawTex, drawW, drawH, image))
                return false;

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
        id<MTLCommandBuffer> cmdBuf = [MtlGfx_queue(ctx) commandBuffer];
        if (cmdBuf == nil)
            return false;

        if (!mtlEncodePresentSource(ctx, pool, cmdBuf, tex, (u32) tex.width, (u32) tex.height, image))
            return false;

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

static fn mtlSetSyncPresent(any context, bool sync)
{
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
static fn mtlDestroyGraphicsContext(any context)
{
    MtlGraphicsContext *ctx = (MtlGraphicsContext*) context;
    if (ctx == nullptr)
        return;

    SecureHandle sh = MtlGfx_selfHandle(ctx);
    mtlDrainPendingPresent(ctx);
    for (u32 i = 0u; i < MTL_MAX_BACKING_IMAGES; i++)
        mtlReleaseBacking(context, i + 1u);

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

    if (MtlGfx_dummyTex(ctx) != nil)
        [MtlGfx_dummyTex(ctx) release];

    if (MtlGfx_dummyBuf(ctx) != nil)
        [MtlGfx_dummyBuf(ctx) release];

    if (MtlGfx_copyBackingNv12Pipe(ctx) != nil)
        [MtlGfx_copyBackingNv12Pipe(ctx) release];

    if (MtlGfx_copyBackingRgbaPipe(ctx) != nil)
        [MtlGfx_copyBackingRgbaPipe(ctx) release];

    if (MtlGfx_presentBackingNv12Pipe(ctx) != nil)
        [MtlGfx_presentBackingNv12Pipe(ctx) release];

    if (MtlGfx_presentBackingRgbaPipe(ctx) != nil)
        [MtlGfx_presentBackingRgbaPipe(ctx) release];

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
    pthread_mutex_destroy(MtlGfx_backingLock(ctx));
    if (SecureHandle_isValid(sh))
        (discard) SecureMemoryArena_free(Graphvex_secureArena(), sh);
}

static id<MTLComputePipelineState> mtlBuildPipeline(id<MTLDevice> dev,
                                                    id<MTLLibrary> lib,
                                                    string fnName)
{
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
                                                         MTLPixelFormat pixelFormat)
{
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

static u32 mtlRegisterShader(any context, string path, string entryName, u32 stage)
{
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

static bool mtlIsMetalName(string s)
{
    if (s == nullptr)
        return false;

    return s[0] == 'm' && s[1] == 'e' && s[2] == 't' && s[3] == 'a' && s[4] == 'l' && s[5] == '\0';
}

static MtlGraphicsContext *MtlGraphicsContext_init(ImagePool *pool)
{
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> queue = nil;
    bool ownsDevice = false;

    ImagePoolDriver poolDriver = ImagePool_driver(pool);
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
    id<MTLComputePipelineState> presentBackingRgbaPipe = mtlBuildPipeline(dev, lib, "graphvex_present_backing_rgba");
    id<MTLComputePipelineState> presentBackingNv12Pipe = mtlBuildPipeline(dev, lib, "graphvex_present_backing_nv12");
    id<MTLComputePipelineState> copyBackingRgbaPipe = mtlBuildPipeline(dev, lib, "graphvex_copy_backing_rgba");
    id<MTLComputePipelineState> copyBackingNv12Pipe = mtlBuildPipeline(dev, lib, "graphvex_copy_backing_nv12");

    MTLTextureDescriptor *dummyDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:1u
                                    height:1u
                                 mipmapped:NO];
    dummyDesc.storageMode = MTLStorageModeShared;
    dummyDesc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> dummyTex = [dev newTextureWithDescriptor:dummyDesc];
    u8 zeroPx[16];
    memset(zeroPx, 0, sizeof(zeroPx));
    id<MTLBuffer> dummyBuf = [dev newBufferWithBytes:zeroPx length:sizeof(zeroPx) options:MTLResourceStorageModeShared];

    if (clearPipe == nil || copyPipe == nil || copyToTexPipe == nil || presentPipe == nil ||
        presentBackingRgbaPipe == nil || presentBackingNv12Pipe == nil ||
        copyBackingRgbaPipe == nil || copyBackingNv12Pipe == nil ||
        dummyTex == nil || dummyBuf == nil) {
        if (dummyBuf != nil)
            [dummyBuf release];
        if (dummyTex != nil)
            [dummyTex release];
        if (copyBackingNv12Pipe != nil)
            [copyBackingNv12Pipe release];
        if (copyBackingRgbaPipe != nil)
            [copyBackingRgbaPipe release];
        if (presentBackingNv12Pipe != nil)
            [presentBackingNv12Pipe release];
        if (presentBackingRgbaPipe != nil)
            [presentBackingRgbaPipe release];
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

        [dummyBuf release];
        [dummyTex release];
        [copyBackingNv12Pipe release];
        [copyBackingRgbaPipe release];
        [presentBackingNv12Pipe release];
        [presentBackingRgbaPipe release];
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
    MtlGfx_setPresentBackingRgbaPipe(ctx, presentBackingRgbaPipe);
    MtlGfx_setPresentBackingNv12Pipe(ctx, presentBackingNv12Pipe);
    MtlGfx_setCopyBackingRgbaPipe(ctx, copyBackingRgbaPipe);
    MtlGfx_setCopyBackingNv12Pipe(ctx, copyBackingNv12Pipe);
    MtlGfx_setDummyTex(ctx, dummyTex);
    MtlGfx_setDummyBuf(ctx, dummyBuf);
    MtlGfx_setOwnsDevice(ctx, ownsDevice);
    pthread_mutex_init(MtlGfx_backingLock(ctx), nullptr);
    return ctx;
}

;;CONSTRUCTOR
Graphics *Graphics_metal(ImagePool *pool)
{
    if (pool == nullptr)
        return nullptr;

    MtlGraphicsContext *ctx = MtlGraphicsContext_init(pool);
    if (ctx == nullptr)
        return nullptr;

    GraphicsDriver driver;
    memset(&driver, 0, sizeof(GraphicsDriver));
    driver.name = "metal";
    driver.context = ctx;
    driver.registerShader = mtlRegisterShader;
    driver.submitCommands = mtlSubmitCommands;
    driver.submitToSurface = mtlSubmitToSurface;
    driver.presentImage = mtlPresentImage;
    driver.setSyncPresent = mtlSetSyncPresent;
    driver.setSurfacePurgeable = mtlSetSurfacePurgeable;
    driver.importBacking = mtlImportBacking;
    driver.allocBacking = mtlAllocBacking;
    driver.releaseBacking = mtlReleaseBacking;
    driver.setBackingPurgeable = mtlSetBackingPurgeable;
    driver.isBackingInUse = mtlIsBackingInUse;
    driver.attachPool = mtlAttachPool;
    driver.detachPool = mtlDetachPool;
    driver.destroyContext = mtlDestroyGraphicsContext;

    Graphics *g = Graphics(pool, &driver);
    if (g == nullptr)
        mtlDestroyGraphicsContext(ctx);

    return g;
}
