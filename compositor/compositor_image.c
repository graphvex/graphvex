#include "vex.h"
#include "compositor/compositor_image.h"

#include <math.h>

;;DEFINITION
/* Explicit Image boundary for the CPU compositor. Borrowed
 * straight-sRGB RGBA8 CPU shadows become owned origin-aware linear-premult
 * surfaces; export reverses that conversion into a newly owned Image. No GPU
 * readback, submission, lifetime retention, or implicit color policy. */
;;OVERVIEW
/* Public: fromImage/toImage with failure-atomic out parameters.
 * Private: srgbDecode/srgbEncode implement the transfer function; export
 * clamps HDR only at the byte boundary. No class fields owned by this adapter. */

// Converts one normalized sRGB channel to linear light.
static f32 srgbDecode(f32 encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f :
        powf((encoded + 0.055f) / 1.055f, 2.4f);
}
// Converts one linear channel to an sRGB byte with endpoint clamping and rounding.
static u8 srgbEncode(f32 linear) {
    if (linear <= 0)
        return 0;
    if (linear >= 1)
        return 255;
    f32 encoded = linear <= 0.0031308f ? linear * 12.92f :
        1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
    return (u8) lroundf(encoded * 255.0f);
}

// Imports a borrowed RGBA8 CPU shadow as an owned linear-premultiplied surface at x,y.
CompositorStatus CompositorSurface_fromImage(const Image *image,
                                             i32 x, i32 y,
                                             CompositorSurface **out) {
    if (!out || !Image_isValid(image))
        return COMPOSITOR_INVALID;
    if (Image_format(image) != IMAGE_FORMAT_RGBA8)
        return COMPOSITOR_UNSUPPORTED;
    const u8 *pixels = Image_pixels(image);
    if (!pixels)
        return COMPOSITOR_INVALID;
    CompositorBounds b = {x, y, Image_width(image), Image_height(image)};
    CompositorSurface *surface = nullptr;
    CompositorStatus status = CompositorSurface_create(b, &surface);
    if (status != COMPOSITOR_OK)
        return status;
    bytes stride = Image_stride(image);
    if (stride < (bytes) b.width * 4 ||
        (b.height > 1 && stride > (SIZE_MAX - (bytes) b.width * 4) / (b.height - 1))) {
        CompositorSurface_destroy(surface);
        return COMPOSITOR_LIMIT;
    }
    f32 *dest = CompositorSurface_pixels(surface);
    for (u32 iy = 0; iy < b.height; ++iy) {
        const u8 *row = pixels + (bytes) iy * stride;
        for (u32 ix = 0; ix < b.width; ++ix) {
            const u8 *p = row + (bytes) ix * 4;
            f32 *q = dest + 4 * ((bytes) iy * b.width + ix);
            f32 alpha = p[3] / 255.0f;
            for (unsigned c = 0; c < 3; ++c)
                q[c] = srgbDecode(p[c] / 255.0f) * alpha;
            q[3] = alpha;
        }
    }
    *out = surface;
    return COMPOSITOR_OK;
}

// Exports a validated surface to a newly owned RGBA8 Image, converting back to straight sRGB.
CompositorStatus CompositorSurface_toImage(const CompositorSurface *surface,
                                           Image **out) {
    if (!out)
        return COMPOSITOR_INVALID;
    CompositorStatus status = CompositorSurface_validate(surface);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorBounds b = CompositorSurface_bounds(surface);
    if (!b.width)
        return COMPOSITOR_INVALID;
    Image *image = Image_2(b.width, b.height);
    if (!image)
        return COMPOSITOR_NO_MEMORY;
    if (!Image_ensureShadow(image, b.width, b.height)) {
        Image_destroy(image);
        return COMPOSITOR_NO_MEMORY;
    }
    const f32 *source = CompositorSurface_constPixels(surface);
    u8 *pixels = Image_pixels(image);
    bytes stride = Image_stride(image);
    for (u32 y = 0; y < b.height; ++y) {
        for (u32 x = 0; x < b.width; ++x) {
            const f32 *p = source + 4 * ((bytes) y * b.width + x);
            u8 *q = pixels + (bytes) y * stride + (bytes) x * 4;
            for (unsigned c = 0; c < 3; ++c)
                q[c] = p[3] > 0 ? srgbEncode(p[c] / p[3]) : 0;
            q[3] = (u8) lroundf(p[3] * 255.0f);
        }
    }
    *out = image;
    return COMPOSITOR_OK;
}
