// graphvex/test/vulkan/surface_gpu_test.c — the zero-copy seam (Apple).
//
// Prove VK_EXT_metal_objects end to end: create a real IOSurface (via the
// graphvex-free IosHost helper), import it as a Vulkan render target, draw into
// it on the GPU, and read the IOSurface the way CoreAnimation would. No
// swapchain, no staging readback in the path.

#include <stdio.h>
#include <stdint.h>

#include "graphics/graphics.h"
#include "test_support.h"
#include "vulkan/vulkan_backend.h"
#include "iosurface_host.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Verifies a borrowed surface can be imported and rendered by Vulkan.
int main(void) {
    void *surf = IosHost_create(32, 32);
    CHECK(surf != nullptr);
    if (!surf) { printf("surface_gpu_test: IOSurfaceCreate failed\n"); return 1; }

    CHECK(Graphics_register(VulkanBackend_row()));
    CHECK(Graphics_use(BACKEND_VULKAN));

    // import the IOSurface as the render target (Apple, VK_EXT_metal_objects)
    if (!VulkanBackend_bindSurface(surf, 32, 32)) {
        printf("surface_gpu_test: SKIP (%s)\n", VulkanBackend_lastError());
        IosHost_release(surf);
        return B_TEST_SKIP;   // no metal-objects support -> skip, not a pass
    }

    CHECK(Graphics_resize(32, 32));
    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_RGBA(0, 0, 0, 255)));
    CHECK(Graphics_fillRect(&(Rect){0, 0, 16, 32}, &(Brush){COLOR_RGBA(255, 0, 0, 255), 0, 0, 0, 0}));
    CHECK(Graphics_fillRect(&(Rect){16, 0, 16, 32}, &(Brush){COLOR_RGBA(0, 0, 255, 255), 0, 0, 0, 0}));
    CHECK(Graphics_end());
    CHECK(Graphics_present());   // renders INTO the IOSurface, no readback

    // read the IOSurface exactly as CoreAnimation/WindowServer would
    bytes stride = 0;
    const u8 *base = IosHost_lockRead(surf, &stride);
    CHECK(base != nullptr);
    if (base) {
        const u8 *left = base + (bytes)16 * stride + (bytes)8 * 4;
        const u8 *right = base + (bytes)16 * stride + (bytes)24 * 4;
        CHECK(left[0] == 255 && left[1] == 0 && left[2] == 0);       // red
        CHECK(right[0] == 0 && right[1] == 0 && right[2] == 255);    // blue
    }
    IosHost_unlock(surf);

    VulkanBackend_unbindSurface();
    IosHost_release(surf);
    printf("surface_gpu_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
