#ifndef GRAPHVEX_MTL_INTERNAL_H
#define GRAPHVEX_MTL_INTERNAL_H

/**
 * metal/mtl_internal.h -- private shared Metal device/slab structs between
 * mtl_vpool.m and mtl_graphics.m. Never included outside metal/.
 */

#include "vex.h"
#include "vio/vio_internal.h"

#if !defined(VEX_PLATFORM_APPLE)
#error "graphvex/metal/mtl_internal.h requires VEX_PLATFORM_APPLE"
#endif

#import <Metal/MTLDevice.h>
#import <Metal/MTLBuffer.h>
#import <Metal/MTLCommandBuffer.h>
#import <Metal/MTLCommandQueue.h>

typedef struct MtlPoolContext {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    bool uma;
} MtlPoolContext;

typedef struct MtlSlabHandle {
    id<MTLBuffer> buffer;
    any mmapBase;
    bytes mmapLen;
} MtlSlabHandle;

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
static inline any MtlVideoSlab_driverHandle(const VideoSlabInfo *s)
{
    return (*s).driverHandle;
}

#endif
