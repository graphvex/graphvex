// graphvex/test/image_test.c — mirrors src/image.c
//
// RGBA8 pixel buffer with a CPU shadow: construct, resize, upload, fill,
// stride, layer, and the opaque native/IOSurface slots.

#include "vex.h"
#include <stdio.h>
#include <string.h>

#include "graphvex/lang/image.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

static unsigned references, releases;
static bool refuseRetain, refuseRelease;
// Test callback that retains a resource unless rejection is requested.
static bool retainResource(any resource) {
    CHECK(resource == &references);
    if (refuseRetain)
        return false;
    ++references;
    return true;
}
// Test callback that releases a resource unless rejection is requested.
static bool releaseResource(any resource) {
    CHECK(resource == &references);
    if (refuseRelease)
        return false;
    --references;
    ++releases;
    return true;
}

// Exercises image ownership callbacks, pixel operations, and invalid inputs.
int main() {
    Image *gpu = Image(2, 3);
    CHECK(!Image_isDrawable(gpu));
    CHECK(!Image_bindGpu(gpu, nullptr, gpu, gpu, retainResource, releaseResource));
    CHECK(!Image_bindGpu(gpu, &references, gpu, gpu, nullptr, releaseResource));
    refuseRetain = true;
    CHECK(!Image_bindGpu(gpu, &references, gpu, gpu, retainResource, releaseResource));
    CHECK(!Image_gpuResource(gpu) && !references);
    refuseRetain = false;
    CHECK(Image_bindGpu(gpu, &references, gpu, gpu, retainResource, releaseResource));
    CHECK(Image_isDrawable(gpu) && !Image_pixels(gpu));
    CHECK(Image_gpuResource(gpu) == &references && Image_gpuDevice(gpu) == gpu && Image_gpuDescriptor(gpu) == gpu);
    CHECK(Image_bindGpu(gpu, &references, gpu, gpu, retainResource, releaseResource));
    CHECK(references == 1); // idempotent binding
    refuseRelease = true;
    CHECK(!Image_clearGpu(gpu) && Image_isDrawable(gpu));
    CHECK(!Image_resize(gpu, 4, 4) && Image_width(gpu) == 2);
    Image_destroy(gpu); // failed release preserves object for retry
    CHECK(Image_isDrawable(gpu) && references == 1);
    refuseRelease = false;
    Image_fill(gpu, COLOR_WHITE); // CPU edit invalidates binding exactly once
    CHECK(Image_pixels(gpu) && !Image_gpuResource(gpu) && !references && releases == 1);
    Image_destroy(gpu);
    CHECK(!Image_clearGpu(nullptr) && !Image_isDrawable(nullptr));
    CHECK(!Image_gpuResource(nullptr) && !Image_gpuDevice(nullptr) && !Image_gpuDescriptor(nullptr));
    // defaults: 1x1 RGBA8, no shadow, no native handle
    Image *img = Image_0();
    CHECK(img != nullptr);
    CHECK(Image_width(img) == 1 && Image_height(img) == 1);
    CHECK(Image_format(img) == IMAGE_FORMAT_RGBA8);
    CHECK(Image_usage(img) == IMAGE_USAGE_NONE);
    CHECK(Image_pixels(img) == nullptr);
    CHECK(Image_isValid(img));
    CHECK(Image_native(img) == nullptr && Image_iosurface(img) == nullptr);

    // 0 dimensions clamp to 1
    Image *z = Image_2(0, 0);
    CHECK(Image_width(z) == 1 && Image_height(z) == 1);
    Image_destroy(z);

    // shadow allocation + stride
    CHECK(Image_ensureShadow(img, 4, 3));
    CHECK(Image_pixels(img) != nullptr);
    CHECK(Image_stride(img) == 16);
    CHECK(Image_width(img) == 4 && Image_height(img) == 3);

    // fill uses monotonic 0xRRGGBBAA byte order
    Image_fill(img, COLOR_RGBA(10, 20, 30, 255));
    const u8 *p = Image_pixels(img);
    CHECK(p[0] == 10 && p[1] == 20 && p[2] == 30 && p[3] == 255);

    // upload replaces content and dims
    u8 src[2 * 2 * 4];
    for (int i = 0; i < 16; i++) src[i] = (u8)i;
    CHECK(Image_upload(src, 2, 2, img));
    CHECK(Image_width(img) == 2 && Image_height(img) == 2);
    CHECK(Image_pixels(img)[3] == 3);

    // resize preserves a block and re-zeroes
    CHECK(Image_resize(img, 3, 3));
    CHECK(Image_width(img) == 3 && Image_height(img) == 3);

    // usage as declared
    Image *u = Image_4(2, 2, IMAGE_FORMAT_BGRA8, IMAGE_USAGE_RENDER | IMAGE_USAGE_TRANSFER);
    CHECK(Image_format(u) == IMAGE_FORMAT_BGRA8);
    CHECK(Image_usage(u) == (IMAGE_USAGE_RENDER | IMAGE_USAGE_TRANSFER));
    Image_destroy(u);

    // layer + native slots
    CHECK(Image_layer(img) == 0u);
    Image_setLayer(img, 3);
    CHECK(Image_layer(img) == 3u);
    Image_setNative(img, (any) 0x1234);
    CHECK(Image_native(img) == (any) 0x1234);
    Image_setIOSurface(img, (any) 0x5678);
    CHECK(Image_iosurface(img) == (any) 0x5678);

    Image_destroy(img);
    Image_destroy(nullptr);   // null-safe
    CHECK(!Image_isValid(nullptr));

    // null-safe reads
    CHECK(Image_width(nullptr) == 0u);
    CHECK(Image_pixels(nullptr) == nullptr);
    CHECK(Image_native(nullptr) == nullptr);
    Image_fill(nullptr, COLOR_WHITE);              // must not crash
    CHECK(!Image_upload(nullptr, 1, 1, nullptr));

    printf("image_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
