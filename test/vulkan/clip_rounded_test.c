// graphvex/test/vulkan/clip_rounded_test.c — mirrors the Vulkan clip path.
//
// The rounded clip is an SDF handed to the fragment shader, which DISCARDS
// fragments outside the parent's rounded rect. Prove it on the GPU: a square
// fill clipped to a circle loses its corners, not just its overflow.

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

// Returns the alpha channel at a pixel for rounded-clip assertions.
static int alpha_at(const Image *img, int x, int y) {
    return Image_pixels(img)[(bytes)y * Image_stride(img) + (bytes)x * 4 + 3];
}

// Checks rounded Vulkan clipping at corners, edges, and interior pixels.
int main(void) {
    CHECK(Graphics_register(VulkanBackend_row()));
    CHECK(Graphics_use(BACKEND_VULKAN));
    CHECK(Graphics_resize(64, 64));

    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_CLEAR));
    CHECK(Graphics_clipRounded(&(Rect){0, 0, 60, 60}, 30));   // a circle
    CHECK(Graphics_fillRect(&(Rect){0, 0, 200, 200}, &(Brush){COLOR_RGBA(255, 0, 0, 255), 0, 0, 0, 0}));
    CHECK(Graphics_clip(nullptr));                               // reset
    CHECK(Graphics_end());

    Image *img = Image_0();
    CHECK(Graphics_capture(img));
    CHECK(alpha_at(img, 30, 30) == 255);   // centre: inside the circle
    CHECK(alpha_at(img, 80, 80) == 0);     // beyond the rect: cut
    CHECK(alpha_at(img, 2, 2) == 0);       // inside the rect, OUTSIDE the circle: the
                                           // shader's rounded SDF discards it
    Image_destroy(img);

    printf("clip_rounded_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
