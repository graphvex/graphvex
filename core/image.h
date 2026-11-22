#ifndef GRAPHVEX_CORE_IMAGE_H
#define GRAPHVEX_CORE_IMAGE_H

/** core/image.h -- vpointer-backed 2D buffer image with mandatory bindless slot. */

#include "core/pixel.h"
#include "vio/vio.h"

;;CLASSNAME("GraphicsImage")

typedef enum GraphicsImageFlag {
    GRAPHICS_IMAGE_NONE    = 0u,
    GRAPHICS_IMAGE_SUBVIEW = 1u << 0,
} GraphicsImageFlag;

typedef struct GraphicsImage {
    vpointer data;
    u32 width;
    u32 height;
    u32 strideBytes;
    u32 bindlessSlot;
    PixelFormat format;
    u32 flags;
} GraphicsImage;

_Static_assert(sizeof(GraphicsImage) == 32, "GraphicsImage obeys 32-byte floor");

;;GETTER
vpointer GraphicsImage_data(const GraphicsImage *self);
;;GETTER
u32 GraphicsImage_width(const GraphicsImage *self);
;;GETTER
u32 GraphicsImage_height(const GraphicsImage *self);
;;GETTER
u32 GraphicsImage_strideBytes(const GraphicsImage *self);
;;GETTER
u32 GraphicsImage_bindlessSlot(const GraphicsImage *self);
;;GETTER
PixelFormat GraphicsImage_format(const GraphicsImage *self);
;;GETTER
u32 GraphicsImage_flags(const GraphicsImage *self);

bool GraphicsImage_isValid(const GraphicsImage *self);
bool GraphicsImage_isSubview(const GraphicsImage *self);
bytes GraphicsImage_byteSize(const GraphicsImage *self);
fn GraphicsImage_fitRect(const GraphicsImage *self, f32 dstX, f32 dstY, f32 dstW, f32 dstH,
                         f32 *outX, f32 *outY, f32 *outW, f32 *outH);

#endif
