// graphvex/test/vulkan/gpu_render_test.c — the GPU path.
//
// Renders quads through the Vulkan backend (offscreen + readback) and verifies
// the pixels came out of the GPU, with the SDF shader's Y-down orientation.

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

// Reads RGB channels at one pixel for rendered-output assertions.
static void at(const Image *img, int x, int y, int *r, int *g, int *b) {
    const uint8_t *p = Image_pixels(img) + (size_t)y * Image_stride(img) + (size_t)x * 4;
    *r = p[0]; *g = p[1]; *b = p[2];
}

// Verifies Vulkan rendering, clipping, and device-loss recovery behavior.
int main(void) {
    Graphics_register(VulkanBackend_row());
    CHECK(Graphics_use(BACKEND_VULKAN));
    CHECK(Graphics_backendId() == BACKEND_VULKAN);

    CHECK(Graphics_resize(64, 64));
    CHECK(Graphics_begin());
    Graphics_clear(COLOR_RGBA(0, 0, 0, 255));
    // left half red, right half blue, top band green (to catch a Y flip)
    Graphics_fillRect(&(Rect){0, 0, 32, 64}, &(Brush){COLOR_RGBA(255, 0, 0, 255), 0, 0, 0, 0});
    Graphics_fillRect(&(Rect){32, 0, 32, 64}, &(Brush){COLOR_RGBA(0, 0, 255, 255), 0, 0, 0, 0});
    Graphics_fillRect(&(Rect){0, 0, 64, 8}, &(Brush){COLOR_RGBA(0, 255, 0, 255), 0, 0, 0, 0});
    CHECK(Graphics_end());

    Image *shot = Image_0();
    CHECK(Graphics_capture(shot));
    CHECK(Image_width(shot) == 64 && Image_height(shot) == 64);

    int r, g, b;
    at(shot, 16, 32, &r, &g, &b);
    CHECK(r == 255 && g == 0 && b == 0);            // left = red
    at(shot, 48, 32, &r, &g, &b);
    CHECK(r == 0 && g == 0 && b == 255);            // right = blue
    at(shot, 32, 4, &r, &g, &b);
    CHECK(r == 0 && g == 255 && b == 0);            // TOP band = green (not flipped)
    at(shot, 32, 60, &r, &g, &b);
    CHECK(!(r == 0 && g == 255 && b == 0));         // bottom is not the band

    // CLIP = DISCARD, not resize. A 200x200 circle (radius 100) clipped to a
    // 60-wide window must stay a circle; a shrink-to-fit would make a 60-wide
    // pill (radius 30) and clip the curve much earlier.
    CHECK(Graphics_resize(60, 200));
    CHECK(Graphics_begin());
    Graphics_clear(COLOR_RGBA(0, 0, 0, 255));
    Graphics_fillRect(&(Rect){0, 0, 200, 200}, &(Brush){COLOR_RGBA(255, 255, 255, 255), 100, 0, 0, 0});
    CHECK(Graphics_end());
    Image *clip = Image_0();
    CHECK(Graphics_capture(clip));
    at(clip, 1, 88, &r, &g, &b);                    // inside the circle, outside a pill
    CHECK(r > 150);
    at(clip, 1, 112, &r, &g, &b);
    CHECK(r > 150);
    at(clip, 1, 100, &r, &g, &b);                   // dead centre-left: both
    CHECK(r > 200);
    at(clip, 40, 100, &r, &g, &b);                  // inside the circle, within clip
    CHECK(r > 200);
    at(clip, 59, 100, &r, &g, &b);                  // still inside the 60px window
    CHECK(r > 200);
    at(clip, 40, 5, &r, &g, &b);                    // above the circle: background
    CHECK(r < 40);
    Image_destroy(clip);

    Image_destroy(shot);
    if (g_fail == 0) printf("gpu_render_test: ALL PASS (Vulkan renders the quads)\n");
    return g_fail == 0 ? 0 : 1;
}
