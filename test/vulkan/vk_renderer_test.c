// graphvex/test/vulkan/vk_renderer_test.c — mirrors src/vulkan/vk_renderer.c
//
// The Vulkan Backend row: display-list verbs land in the CPU quad batch, and
// capture renders them ON THE GPU (offscreen) + reads back. clear() is the
// render-pass clear (no quad); clip() intersects into the recorded quad.

#include "vex.h"
#include <stdio.h>

#include "graphics/graphics.h"
#include "lang/image.h"
#include "vulkan/vulkan_backend.h"
#include "vulkan/vk_batch.h"

;;GETTER
static bytes VkBatch_count(const VkBatch *b) { return (*b).count; }
;;GETTER
static const VkQuad *VkBatch_quads(const VkBatch *b) { return (*b).quads; }
;;GETTER
static f32 VkQuad_w(const VkQuad *q) { return (*q).w; }
;;GETTER
static f32 VkQuad_h(const VkQuad *q) { return (*q).h; }
;;GETTER
static f32 VkQuad_cx0(const VkQuad *q) { return (*q).cx0; }
;;GETTER
static f32 VkQuad_cx1(const VkQuad *q) { return (*q).cx1; }
;;GETTER
static f32 VkQuad_cy0(const VkQuad *q) { return (*q).cy0; }
;;GETTER
static f32 VkQuad_cy1(const VkQuad *q) { return (*q).cy1; }

static i32 g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Reads RGB channels from one rendered image pixel.
static fn at(const Image *img, i32 x, i32 y, i32 *r, i32 *g, i32 *b) {
    const u8 *p = Image_pixels(img) + (bytes)y * Image_stride(img) + (bytes)x * 4;
    *r = p[0]; *g = p[1]; *b = p[2];
}

// Verifies Vulkan renderer pixels, clipping, and resource cleanup.
i32 main() {
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
    CHECK(VkBatch_count(batch) == 2);                          // two rects, no clear quad
    CHECK(Graphics_end());

    // capture = GPU render + readback
    Image *shot = Image_0();
    CHECK(Graphics_capture(shot));
    i32 r, g, b;
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
    CHECK(VkBatch_count(b2) == 1);
    const VkQuad *q0 = &VkBatch_quads(b2)[0];
    CHECK(VkQuad_w(q0) == 32.0f && VkQuad_h(q0) == 32.0f);      // shape intact
    CHECK(VkQuad_cx0(q0) == 0.0f && VkQuad_cx1(q0) == 8.0f);    // clip baked local
    CHECK(VkQuad_cy0(q0) == 0.0f && VkQuad_cy1(q0) == 8.0f);
    CHECK(Graphics_end());
    CHECK(VulkanBackend_lastError() != nullptr);

    printf("vk_renderer_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
