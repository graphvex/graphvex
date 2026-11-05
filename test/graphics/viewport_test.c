// graphvex/test/graphics/viewport_test.c — mirrors src/graphics/viewport.c
//
// Points <-> native pixels. There is NO virtual canvas: the UI works in logical
// points at 1x and the backing scale converts to native pixels at the boundary.

#include <stdio.h>

#include "graphics/viewport.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Checks viewport mapping, clipping, and boundary coordinate behavior.
int main(void) {
    Viewport v = Viewport();
    CHECK(Viewport_isEmpty(&v));          // 0x0

    Viewport_resize(&v, 1800, 1200, 2.0f);   // retina: 2x
    CHECK(!Viewport_isEmpty(&v));
    CHECK(v.width == 1800.0f && v.height == 1200.0f);
    CHECK(v.scale == 2.0f);
    CHECK(v.scissor.w == 1800.0f && v.scissor.h == 1200.0f);

    // points -> native px
    CHECK(Viewport_x(&v, 100.0f) == 200.0f);
    CHECK(Viewport_y(&v, 60.0f) == 120.0f);
    CHECK(Viewport_w(&v, 50.0f) == 100.0f);
    CHECK(Viewport_h(&v, 30.0f) == 60.0f);
    Rect r = Viewport_rect(&v, 100, 60, 50, 30);
    CHECK(r.x == 200.0f && r.y == 120.0f && r.w == 100.0f && r.h == 60.0f);

    // native px -> points
    ViewportPoint point = Viewport_toPoints(&v, 200, 120);
    CHECK(point.x == 100.0f && point.y == 60.0f);
    point = Viewport_toPoints(&v, -4, -8);   // negatives are exact too
    CHECK(point.x == -2.0f && point.y == -4.0f);

    // logical size (what the UI lays out in)
    ViewportSize logical = Viewport_logicalSize(&v);
    CHECK(logical.width == 900.0f && logical.height == 600.0f);

    // a 0 backing scale is coerced to 1 (never a divide-by-zero)
    Viewport_resize(&v, 100, 100, 0.0f);
    CHECK(v.scale == 1.0f);
    CHECK(Viewport_x(&v, 7.0f) == 7.0f);

    // null-safe reads
    CHECK(Viewport_isEmpty(nullptr));
    CHECK(Viewport_x(nullptr, 5.0f) == 5.0f);
    point = Viewport_toPoints(nullptr, 10, 20);   // must not crash
    CHECK(point.x == 10.0f && point.y == 20.0f);

    printf("viewport_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
