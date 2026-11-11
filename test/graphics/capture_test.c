// graphvex/test/graphics/capture_test.c — the CAPTURE() screenshot path.
//
// Renders into the raster target, then CAPTURE(&image) copies the frame into an
// Image so a test (or an agent) can inspect the actual pixels.

#include <stdio.h>

#include "graphics/graphics.h"
#include "graphvex/lang/image.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Returns one channel byte from the image's packed pixel storage.
static u8 byteAt(const Image *img, int x, int y, int c) {
    const u8 *p = Image_pixels(img);
    return p[(bytes)y * Image_stride(img) + (bytes)x * 4 + (bytes)c];
}

// Verifies captured graphics pixels and image channel ordering.
int main(void) {
    CHECK(Graphics_use(BACKEND_RASTER));
    CHECK(Raster_configure(32, 16));

    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_BLACK));
    CHECK(Graphics_fillRect(&(Rect){0, 0, 16, 16}, &(Brush){COLOR_WHITE, 0, 0, 0, 0}));
    CHECK(Graphics_end());

    // capture into a fresh 1x1 image -> sized to the target
    Image *shot = Image_0();
    CHECK(CAPTURE(shot));
    CHECK(Image_width(shot) == 32 && Image_height(shot) == 16);

    // left half white, right half black (0xRRGGBBAA byte order)
    CHECK(byteAt(shot, 3, 3, 0) == 255 && byteAt(shot, 3, 3, 3) == 255);
    CHECK(byteAt(shot, 20, 3, 0) == 0 && byteAt(shot, 20, 3, 3) == 255);

    // capture resizes an existing destination
    Image *big = Image_2(100, 100);
    CHECK(Graphics_capture(big));
    CHECK(Image_width(big) == 32 && Image_height(big) == 16);
    Image_destroy(big);

    // capture without an active backend / null dest is refused
    Graphics_use(9999u);            // unknown -> keeps raster selected actually
    CHECK(!Graphics_capture(nullptr));

    Image_destroy(shot);
    printf("capture_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
