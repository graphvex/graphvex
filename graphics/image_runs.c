#include "graphics/image_runs.h"
#include "graphvex/lang/image.h"
#include "util/annotation.h"

#include <limits.h>
#include <math.h>

_Static_assert(SIZE_MAX / 4 >= UINT32_MAX, "RGBA row Bytes require supported 64-bit bytes");

;;DEFINITION
/* ImageRuns converts a borrowed straight RGBA8 CPU shadow into nearest-sampled
 * horizontal solid-color runs for correctness-first backend image rendering. */
;;OVERVIEW
/* Stateless module. Public: ImageRuns_visit. No fields/resources. Private:
 * validRect checks finite positive geometry; sample reads stride-aware RGBA8.
 * Iteration is bounded and synchronous; failed callbacks may leave partial
 * consumer output. This reference path is not production texture-upload proof. */

static bool validRect(Rect rect) {
    return isfinite(rect.x) && isfinite(rect.y) && isfinite(rect.w) &&
           isfinite(rect.h) && rect.w > 0 && rect.h > 0 &&
           isfinite(rect.x + rect.w) && isfinite(rect.y + rect.h);
}

// Reads one RGBA8 sample, returning transparent black outside the image extent.
static Color sample(const u8 *pixels, bytes stride, u32 width,
                    u32 height, Rect destination, i32 x, i32 y) {
    d64 u = ((d64) x + 0.5 - destination.x) / destination.w;
    d64 v = ((d64) y + 0.5 - destination.y) / destination.h;
    u32 sx = (u32) fmin(fmax(floor(u * width), 0), width - 1);
    u32 sy = (u32) fmin(fmax(floor(v * height), 0), height - 1);
    const u8 *pixel = pixels + (bytes) sy * stride + (bytes) sx * 4;
    return COLOR_RGBA(pixel[0], pixel[1], pixel[2], pixel[3]);
}

// Visits contiguous horizontal runs of equal sampled color within the clipped destination.
bool ImageRuns_visit(const Image *image, Rect destination, Rect clip,
                     ImageRunsFn visit, void *context) {
    if (!image || !visit || !Image_isValid(image) || !validRect(destination) ||
        !validRect(clip) || Image_format(image) != IMAGE_FORMAT_RGBA8)
        return false;
    const u8 *pixels = Image_pixels(image);
    u32 width = Image_width(image), height = Image_height(image);
    bytes stride = Image_stride(image);
    if (!pixels || stride < (bytes) width * 4 ||
        (height > 1 && stride > (SIZE_MAX - (bytes) width * 4) / (height - 1)))
        return false;
    Rect visible = Rect_intersect(destination, clip);
    if (Rect_isEmpty(visible))
        return true;
    d64 left = ceil((d64) visible.x - 0.5);
    d64 top = ceil((d64) visible.y - 0.5);
    d64 right = ceil((d64) visible.x + visible.w - 0.5);
    d64 bottom = ceil((d64) visible.y + visible.h - 0.5);
    if (left < INT32_MIN || top < INT32_MIN || right > INT32_MAX || bottom > INT32_MAX ||
        right - left > 16777216 || bottom - top > 16777216 ||
        (right - left) * (bottom - top) > 16777216)
        return false;
    i32 x0 = (i32) left, y0 = (i32) top;
    i32 x1 = (i32) right, y1 = (i32) bottom;
    for (i32 y = y0; y < y1; ++y) {
        i32 start = x0;
        while (start < x1) {
            Color color = sample(pixels, stride, width, height, destination, start, y);
            i32 end = start + 1;
            while (end < x1 && sample(pixels, stride, width, height, destination, end, y) == color)
                ++end;
            if (Color_alpha(color)) {
                Rect run = {(f32) start, (f32) y, (f32) (end - start), 1};
                if (!visit(run, color, context))
                    return false;
            }
            start = end;
        }
    }
    return true;
}
