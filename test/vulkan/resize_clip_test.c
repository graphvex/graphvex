// graphvex/test/vulkan/resize_clip_test.c — resizing CLIPS, never resizes.
//
// The marshmallow rule: when the window shrinks, a panel keeps its size and
// position and the over-pixels are simply cut. So a frame rendered at 150x150
// after a resize must be the exact top-left crop of the same scene rendered at
// 300x300 — pixel for pixel. If the backend "shrinks to fit" (clamps the quad
// to the window and recomputes the corner), this crop equality breaks.

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

// Returns the RGBA byte address for a pixel in the image.
static const u8 *at(const Image *img, int x, int y) {
    return Image_pixels(img) + (bytes)y * Image_stride(img) + (bytes)x * 4;
}

// A stiff scene: a 200x200 panel (radius 24) pinned at the top-left, and a
// second panel straddling the shrink edge so the crop is non-trivial.
// Records the fixed scene used for resize and clipping comparisons.
static fn scene() {
    Graphics_clear(COLOR_RGBA(10, 12, 16, 255));
    Graphics_fillRect(&(Rect){0, 0, 200, 200},
                      &(Brush){COLOR_RGBA(230, 120, 120, 255), 24, 0, 0, 0});
    Graphics_fillRect(&(Rect){120, 40, 200, 200},
                      &(Brush){COLOR_RGBA(120, 180, 230, 255), 32, 0, 0, 0});
}

// Render the scene at a size THROUGH the resize path a window uses, then grab it.
// Renders the shared scene at the requested extent and returns its image.
static Image *render_at(int w, int h) {
    CHECK(Graphics_resize((u32)w, (u32)h));
    CHECK(Graphics_begin());
    scene();
    CHECK(Graphics_end());
    Image *img = Image_0();
    CHECK(Graphics_capture(img));
    return img;
}

// Confirms shrinking and restoring the viewport preserves clipped pixels.
int main() {
    CHECK(Graphics_register(VulkanBackend_row()));
    CHECK(Graphics_use(BACKEND_VULKAN));

    Image *big = render_at(300, 300);
    Image *small = render_at(150, 150);   // shrink through the resize mechanism

    CHECK(Image_width(big) == 300 && Image_height(big) == 300);
    CHECK(Image_width(small) == 150 && Image_height(small) == 150);

    // THE TEST: the smaller frame is the exact top-left crop of the bigger one.
    int diff = 0, firstx = -1, firsty = -1;
    for (int y = 0; y < 150; y++) {
        for (int x = 0; x < 150; x++) {
            const u8 *a = at(big, x, y);
            const u8 *b = at(small, x, y);
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3]) {
                if (diff == 0) { firstx = x; firsty = y; }
                diff++;
            }
        }
    }
    if (diff) printf("  crop mismatch at (%d,%d), %d px differ\n", firstx, firsty, diff);
    CHECK(diff == 0);

    // Directly: the 200-wide panel's straight top edge reaches x=176 (radius 24).
    // A shrink-to-fit 150 box would curl that away before x=126 — so (146,4)
    // must still be panel fill, not background.
    const u8 *p = at(small, 146, 4);
    CHECK(p[0] > 180 && p[1] < 170);              // the red panel, not the 10,12,16 bg
    // ...and the same point in the big frame, proving consistency.
    const u8 *q = at(big, 146, 4);
    CHECK(p[0] == q[0] && p[1] == q[1] && p[2] == q[2]);

    // Beyond the window there is nothing to draw — the crop simply ends.
    CHECK(Image_pixels(small) != nullptr);

    Image_destroy(big);
    Image_destroy(small);
    if (g_fail == 0) printf("resize_clip_test: ALL PASS (resize clips, never resizes)\n");
    return g_fail == 0 ? 0 : 1;
}
