// graphvex/test/graphics/graphics_test.c — mirrors src/graphics/graphics.c
//
// The rect-first core: backend registry, the raster (headless) backend, solid
// fill, rounded corners, alpha blend, clip, and the display list → submit path.

#include <stdio.h>

#include "graphics/graphics.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Checks graphics backend registration and lookup behavior.
static void test_registry(void) {
    CHECK(Graphics_use(BACKEND_RASTER));
    CHECK(Graphics_backendId() == BACKEND_RASTER);
    CHECK(!Graphics_use(9999u));                 // unknown id refused
    CHECK(Graphics_backendId() == BACKEND_RASTER); // previous selection kept
    CHECK(!Graphics_register(nullptr));
    CHECK(Graphics_current() != nullptr && (*Graphics_current()).fillRect != nullptr);
}

// Verifies rectangle fills and clearing on the raster backend.
static void test_fill_and_clear(void) {
    CHECK(Raster_configure(64, 64));
    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_BLACK));
    CHECK(Graphics_fillRect(&(Rect){5, 5, 10, 10}, &(Brush){COLOR_WHITE, 0, 0, 0, 0}));
    CHECK(Graphics_end());
    CHECK(Raster_pixelAt(6, 6) == COLOR_WHITE);
    CHECK(Raster_pixelAt(4, 4) == COLOR_BLACK);
    // out-of-bounds read is 0, never a crash
    CHECK(Raster_pixelAt(1u << 20, 1u << 20) == 0u);
}

// Checks rounded-rectangle corner coverage and interior fill.
static void test_rounded_corners(void) {
    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_BLACK));
    CHECK(Graphics_fillRect(&(Rect){0, 0, 10, 10}, &(Brush){COLOR_WHITE, 5.0f, 0, 0, 0}));
    CHECK(Graphics_end());
    CHECK(Raster_pixelAt(0, 0) == COLOR_BLACK);   // corner carved away
    CHECK(Raster_pixelAt(9, 9) == COLOR_BLACK);
    CHECK(Raster_pixelAt(5, 5) == COLOR_WHITE);   // centre filled
}

// Verifies source-over blending and transparent-source behavior.
static void test_alpha_blend(void) {
    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_BLACK));
    CHECK(Graphics_fillRect(&(Rect){0, 0, 4, 4}, &(Brush){COLOR_RGBA(255, 255, 255, 128), 0, 0, 0, 0}));
    uint32_t p = Raster_pixelAt(1, 1);
    CHECK(Color_red(p) > 100u && Color_red(p) < 160u);
    CHECK(Graphics_end());
    // fully transparent leaves the destination alone
    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_BLACK));
    CHECK(Graphics_fillRect(&(Rect){0, 0, 4, 4}, &(Brush){COLOR_RGBA(255, 0, 0, 0), 0, 0, 0, 0}));
    CHECK(Raster_pixelAt(1, 1) == COLOR_BLACK);
    CHECK(Graphics_end());
}

// Checks display-list recording, clipping, and invalid geometry handling.
static void test_display_list_and_clip(void) {
    DisplayList *dl = DisplayList();
    CHECK(dl != nullptr);
    CHECK(DisplayList_count(dl) == 0);
    DisplayList_rect(dl, (Rect){0, 0, 8, 8}, &(Brush){COLOR_WHITE, 0, 0, 0, 0});
    DisplayList_clip(dl, (Rect){0, 0, 4, 4});
    DisplayList_rect(dl, (Rect){0, 0, 8, 8}, &(Brush){COLOR_RGBA(255, 0, 0, 255), 0, 0, 0, 0});
    DisplayList_unclip(dl);
    CHECK(DisplayList_count(dl) == 4);
    CHECK(DisplayList_cmds(dl) != nullptr);

    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_BLACK));
    CHECK(Graphics_submit(dl));
    CHECK(Graphics_end());
    CHECK(Raster_pixelAt(6, 6) == COLOR_WHITE);                 // outside clip
    CHECK(Raster_pixelAt(2, 2) == COLOR_RGBA(255, 0, 0, 255));  // inside clip
    DisplayList_free(dl);

    // empty rects and null lists are no-ops
    DisplayList *d2 = DisplayList();
    DisplayList_rect(d2, (Rect){0, 0, 0, 5}, &(Brush){COLOR_WHITE, 0, 0, 0, 0});
    CHECK(DisplayList_count(d2) == 0);
    DisplayList_free(d2);
    DisplayList_free(nullptr);   // null-safe
}

// Verifies packed-color helpers and rectangle containment boundaries.
static void test_color_helpers(void) {
    Color c = COLOR_RGBA(0x12, 0x34, 0x56, 0x78);
    CHECK(Color_red(c) == 0x12);
    CHECK(Color_green(c) == 0x34);
    CHECK(Color_blue(c) == 0x56);
    CHECK(Color_alpha(c) == 0x78);
    CHECK(COLOR_BLACK == 0x000000FFu);   // 0xRRGGBBAA
    CHECK(Rect_contains((Rect){0, 0, 4, 4}, 2, 2));
    CHECK(!Rect_contains((Rect){0, 0, 4, 4}, 4, 4));
    CHECK(Rect_isEmpty((Rect){0, 0, 0, 9}));
}

// Runs graphics registry, rasterization, blending, clipping, and color tests.
int main(void) {
    test_registry();
    test_fill_and_clear();
    test_rounded_corners();
    test_alpha_blend();
    test_display_list_and_clip();
    test_color_helpers();
    if (g_fail == 0) printf("graphics_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
