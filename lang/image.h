#ifndef GRAPHICS_IMAGE_H
#define GRAPHICS_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics/graphics.h"

// graphvex R3 — image.h
//
// An RGBA8 pixel buffer. The substrate for scenes, offscreen Boards, and
// pixel-buffered panels. Owns an optional CPU shadow and/or a retained completed
// sampled GPU texture. GPU-only filter outputs remain drawable without readback.
// Legacy native/IOSurface fields are borrowed handles, not ownership references.
//
// Strict 0xRRGGBBAA Color Law: Bytes in memory are R,G,B,A; the Metal drawable
// wants BGRA, so the conversion lives ONLY at the presentation boundary.

#define IMAGE_FORMAT_RGBA8 0u
#define IMAGE_FORMAT_BGRA8 1u

#define IMAGE_USAGE_NONE     0u
#define IMAGE_USAGE_SAMPLED  (1u << 0)
#define IMAGE_USAGE_RENDER   (1u << 1)
#define IMAGE_USAGE_TRANSFER (1u << 2)

typedef struct Image Image;
typedef bool (*ImageGpuRefFn)(any resource);

typedef struct ImageDesc {
    u32 width;    // default 1
    u32 height;   // default 1
    u32 format;   // IMAGE_FORMAT_* (default RGBA8)
    u32 usage;    // IMAGE_USAGE_* (default NONE)
} ImageDesc;

Image *Image_0();
Image *Image_2(u32 width, u32 height);
Image *Image_4(u32 width, u32 height, u32 format, u32 usage);
#define GRAPHVEX_IMAGE_CTOR(_0,_1,_2,_3,_4,NAME,...) NAME
#define Image(...) GRAPHVEX_IMAGE_CTOR(0 __VA_OPT__(,) __VA_ARGS__, \
    Image_4,Image_invalidArity,Image_2,Image_invalidArity,Image_0)(__VA_ARGS__)
Image *Image_new(const ImageDesc *desc);
fn Image_destroy(Image *image);

bool Image_resize(Image *image, u32 width, u32 height);
bool Image_ensureShadow(Image *image, u32 width, u32 height);
bool Image_upload(const u8 *rgba, u32 width, u32 height, Image *dest);
fn Image_fill(Image *image, Color color);   // CPU shadow fill

u32 Image_width(const Image *image);
u32 Image_height(const Image *image);
u32 Image_format(const Image *image);
u32 Image_usage(const Image *image);
bytes   Image_stride(const Image *image);      // Bytes per row
u8 *Image_pixels(const Image *image);      // CPU shadow (nullable)
bool     Image_isValid(const Image *image);

any Image_native(const Image *image);        // dialect handle (VkImage / MTLTexture)
any Image_iosurface(const Image *image);     // IOSurfaceRef when shared
fn   Image_setNative(Image *image, any native);
fn   Image_setIOSurface(Image *image, any ioSurface);
u32 Image_layer(const Image *image);       // atlas layer / sampler index (0 default)
fn     Image_setLayer(Image *image, u32 layer);

/* Owned reference to an optional immutable sampled texture; CPU pixels may be
 * absent. Driver admission validates matching extent and readiness; clearGpu unbinds.
 * Texture Device must outlive Image and any recorded GPU frame. Raw CPU writes
 * must call Image_clearGpu(image) to invalidate a prepared texture;
 * upload/fill/resize do so automatically. Image_destroy retains the object if
 * a final texture release cannot prove upload completion within 100ms. */
/* Backend admission supplies validated ready handles and paired callbacks.
 * This generic ownership seam does not force raster clients to link Vulkan. */
bool Image_bindGpu(Image *image, any resource, any device, any descriptor,
                   ImageGpuRefFn retain, ImageGpuRefFn release);
bool Image_clearGpu(Image *image);
any Image_gpuResource(const Image *image);
any Image_gpuDevice(const Image *image);
any Image_gpuDescriptor(const Image *image);
bool Image_isDrawable(const Image *image);

#endif // GRAPHICS_IMAGE_H
