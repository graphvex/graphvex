/* Owner: compositor/compositor_image.{h,c}. Proves public fromImage/toImage,
 * origin, every byte transfer round trip, transparent black, alpha/gain,
 * stride reuse, explicit BGRA rejection, missing shadows/invalid/null/empty,
 * source borrowing and owned output teardown. CPU-only, no GPU integration.
 * Allocation failure injection and platform alternatives remain gaps. */

#include "vex.h"
#include "../../compositor/compositor_image.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

// Verifies image upload and sampling preserve a complete pixel round trip.
static fn round_trip() {
    Image *image = Image_2(256, 2);
    assert(image && Image_ensureShadow(image, 256, 2));
    u8 *pixels = Image_pixels(image);
    bytes stride = Image_stride(image);
    for (u32 y = 0; y < 2; ++y) {
        for (u32 x = 0; x < 256; ++x) {
            u8 *p = pixels + (bytes) y * stride + (bytes) x * 4;
            p[0] = (u8) x;
            p[1] = (u8) (255 - x);
            p[2] = (u8) (x / 2);
            p[3] = y ? 128 : 255;
        }
    }
    CompositorSurface *surface = nullptr;
    assert(CompositorSurface_fromImage(image, -17, 33, &surface) == COMPOSITOR_OK);
    CompositorBounds bounds = CompositorSurface_bounds(surface);
    assert(bounds.x == -17 && bounds.y == 33 && bounds.width == 256 && bounds.height == 2);
    assert(CompositorSurface_validate(surface) == COMPOSITOR_OK);
    Image *out = nullptr;
    assert(CompositorSurface_toImage(surface, &out) == COMPOSITOR_OK);
    const u8 *exported = Image_pixels(out);
    for (u32 y = 0; y < 2; ++y)
        for (u32 x = 0; x < 256 * 4; ++x)
            assert(exported[(bytes) y * Image_stride(out) + x] == pixels[(bytes) y * stride + x]);
    Image_destroy(out);
    CompositorSurface_destroy(surface);
    /* Grow-only shadow retains stride larger than logical width. */
    assert(Image_resize(image, 1, 2));
    assert(Image_stride(image) == stride);
    assert(CompositorSurface_fromImage(image, 0, 0, &surface) == COMPOSITOR_OK);
    assert(CompositorSurface_toImage(surface, &out) == COMPOSITOR_OK);
    assert(Image_pixels(out)[Image_stride(out) + 3] == 128);
    Image_destroy(out);
    CompositorSurface_destroy(surface);
    Image_destroy(image);
}

// Checks alpha handling and gain adjustments on image composition.
static fn alpha_and_gain() {
    const u8 rgba[] = {255, 128, 64, 128, 255, 200, 100, 0};
    Image *image = Image_2(2, 1);
    assert(Image_upload(rgba, 2, 1, image));
    CompositorSurface *surface = nullptr;
    assert(CompositorSurface_fromImage(image, 0, 0, &surface) == COMPOSITOR_OK);
    const f32 *p = CompositorSurface_constPixels(surface);
    assert(fabsf(p[0] - 128.0f / 255) < 1e-7f);
    assert(fabsf(p[1] - .2158605f * (128.0f / 255)) < 1e-6f);
    assert(p[4] == 0 && p[5] == 0 && p[6] == 0 && p[7] == 0);
    Image_destroy(image); /* imported source is no longer borrowed */
    const CompositorSurface *sources[] = {surface};
    FilterToken gain = Filter_gain(2);
    CompositorSurface *filtered = nullptr;
    assert(Compositor_compose(sources, 1, &gain, 1, &filtered) == COMPOSITOR_OK);
    Image *out = nullptr;
    assert(CompositorSurface_toImage(filtered, &out) == COMPOSITOR_OK);
    const u8 *q = Image_pixels(out);
    assert(q[0] == 255 && q[1] == 176 && q[3] == 128);
    assert(q[4] == 0 && q[5] == 0 && q[6] == 0 && q[7] == 0);
    Image_destroy(out);
    CompositorSurface_destroy(filtered);
    CompositorSurface_destroy(surface);
}

// Checks invalid image inputs and preserved state after rejection.
static fn failures() {
    CompositorSurface *empty = nullptr;
    assert(CompositorSurface_create((CompositorBounds) {0}, &empty) == COMPOSITOR_OK);
    CompositorSurface *out = empty;
    assert(CompositorSurface_fromImage(nullptr, 0, 0, &out) == COMPOSITOR_INVALID);
    Image *image = Image_2(1, 1);
    assert(CompositorSurface_fromImage(image, 0, 0, &out) == COMPOSITOR_INVALID);
    assert(CompositorSurface_fromImage(image, 0, 0, nullptr) == COMPOSITOR_INVALID);
    Image_destroy(image);
    image = Image_4(1, 1, IMAGE_FORMAT_BGRA8, 0);
    assert(CompositorSurface_fromImage(image, 0, 0, &out) == COMPOSITOR_UNSUPPORTED);
    assert(out == empty);
    Image *unchanged = image;
    assert(CompositorSurface_toImage(nullptr, &unchanged) == COMPOSITOR_INVALID);
    assert(CompositorSurface_toImage(empty, &unchanged) == COMPOSITOR_INVALID);
    assert(CompositorSurface_toImage(empty, nullptr) == COMPOSITOR_INVALID);
    assert(unchanged == image);
    Image_destroy(image);
    CompositorSurface_destroy(empty);
}

// Runs image round-trip, alpha/gain, and failure-contract scenarios.
int main() {
    round_trip();
    alpha_and_gain();
    failures();
    puts("compositor Image color/alpha/stride/ownership contracts: PASS");
    return 0;
}
