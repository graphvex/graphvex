// graphvex/test/vulkan/vk_renderer_test.c — mirrors src/vulkan/vk_renderer.c
//
// The Vulkan Backend row: display-list verbs land in the CPU quad batch, and
// capture renders them ON THE GPU (offscreen) + reads back. clear() is the
// render-pass clear (no quad); clip() intersects into the recorded quad.

#include <stdio.h>

#include "graphics/graphics.h"
#include "graphvex/lang/image.h"
#include "vulkan/vulkan_backend.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Reads RGB channels from one rendered image pixel.
static void at(const Image *img, int x, int y, int *r, int *g, int *b) {
    const u8 *p = Image_pixels(img) + (bytes)y * Image_stride(img) + (bytes)x * 4;
    *r = p[0]; *g = p[1]; *b = p[2];
}

// Verifies Vulkan renderer pixels, clipping, and resource cleanup.
int main(void) {
    CHECK(Graphics_register(VulkanBackend_row()));
    CHECK(Graphics_use(BACKEND_VULKAN));
    CHECK(Graphics_backendId() == BACKEND_VULKAN);

    CHECK(Graphics_resize(32, 32));
    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_RGBA(0, 0, 0, 255)));   // render-pass clear
    CHECK(Graphics_fillRect(&(Rect){0, 0, 16, 32}, &(Brush){COLOR_RGBA(255, 0, 0, 255), 0, 0, 0, 0}));
    CHECK(Graphics_fillRect(&(Rect){16, 0, 16, 32}, &(Brush){COLOR_RGBA(0, 0, 255, 255), 0, 0, 0, 0}));

    const VkBatch *batch = VulkanBackend_batch();
    CHECK(batch != nullptr);
    CHECK((*batch).count == 2);                          // two rects, no clear quad
    CHECK(Graphics_end());

    // capture = GPU render + readback
    Image *shot = Image_0();
    CHECK(Graphics_capture(shot));
    int r, g, b;
    at(shot, 8, 16, &r, &g, &b);
    CHECK(r == 255 && g == 0 && b == 0);
    at(shot, 24, 16, &r, &g, &b);
    CHECK(b == 255 && r == 0);
    Image_destroy(shot);

    // clip DISCARDS pixels — the quad keeps its true geometry (never shrinks)
    CHECK(Graphics_begin());
    CHECK(Graphics_clip(&(Rect){0, 0, 8, 8}));
    CHECK(Graphics_fillRect(&(Rect){0, 0, 32, 32}, &(Brush){COLOR_WHITE, 0, 0, 0, 0}));
    const VkBatch *b2 = VulkanBackend_batch();
    CHECK((*b2).count == 1);
    CHECK((*b2).quads[0].w == 32.0f && (*b2).quads[0].h == 32.0f);      // shape intact
    CHECK((*b2).quads[0].cx0 == 0.0f && (*b2).quads[0].cx1 == 8.0f);    // clip baked local
    CHECK((*b2).quads[0].cy0 == 0.0f && (*b2).quads[0].cy1 == 8.0f);
    CHECK(Graphics_end());
    CHECK(VulkanBackend_lastError() != nullptr);

    printf("vk_renderer_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
