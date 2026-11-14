// graphvex/test/element_test.c — mirrors src/element.c
//
// The UI node (Element): anchor/pivot placement across ALL 9 parts x 9 pivots,
// plus offsets, styles (radius/border/shadow) and the paint path.

#include "vex.h"
#include <math.h>
#include <stdio.h>

#include "ui/element.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Compares layout coordinates using the test's f32 tolerance.
static int closef(f32 a, f32 b) { return fabsf(a - b) < 0.001f; }

// Exercises element construction, anchors, pivots, painting, and null guards.
int main() {
    Rect parent = {100, 50, 200, 100};

    // --- the 9 parts themselves ---
    Rect q = {0, 0, 100, 60};
    CHECK((Part_point(q, PART_TOP_LEFT).x == 0 && Part_point(q, PART_TOP_LEFT).y == 0));
    CHECK((Part_point(q, PART_TOP_CENTER).x == 50 && Part_point(q, PART_TOP_CENTER).y == 0));
    CHECK((Part_point(q, PART_TOP_RIGHT).x == 100 && Part_point(q, PART_TOP_RIGHT).y == 0));
    CHECK((Part_point(q, PART_MIDDLE_LEFT).x == 0 && Part_point(q, PART_MIDDLE_LEFT).y == 30));
    CHECK((Part_point(q, PART_CENTER).x == 50 && Part_point(q, PART_CENTER).y == 30));
    CHECK((Part_point(q, PART_MIDDLE_RIGHT).x == 100 && Part_point(q, PART_MIDDLE_RIGHT).y == 30));
    CHECK((Part_point(q, PART_BOTTOM_LEFT).x == 0 && Part_point(q, PART_BOTTOM_LEFT).y == 60));
    CHECK((Part_point(q, PART_BOTTOM_CENTER).x == 50 && Part_point(q, PART_BOTTOM_CENTER).y == 60));
    CHECK((Part_point(q, PART_BOTTOM_RIGHT).x == 100 && Part_point(q, PART_BOTTOM_RIGHT).y == 60));
    // an out-of-range part clamps to TOP_LEFT, never a bad read
    CHECK((Part_point(q, 999).x == 0 && Part_point(q, -3).y == 0));

    ElementDesc d = {0};
    d.width = 20;
    d.height = 10;
    d.anchor = PART_TOP_LEFT;
    d.pivot = PART_TOP_LEFT;
    Element *p = Element(&d);
    CHECK(p != nullptr && Element_isValid(p));

    // --- specific known placements ---
    Rect r = Element_resolve(p, parent);
    CHECK(closef(r.x, 100) && closef(r.y, 50));

    Element_setAnchor(p, PART_CENTER);
    Element_setPivot(p, PART_CENTER);
    r = Element_resolve(p, parent);
    CHECK(closef(r.x, 190) && closef(r.y, 95));      // centred

    Element_setAnchor(p, PART_BOTTOM_RIGHT);
    Element_setPivot(p, PART_BOTTOM_RIGHT);
    r = Element_resolve(p, parent);
    CHECK(closef(r.x, 280) && closef(r.y, 140));

    Element_setAnchor(p, PART_BOTTOM_RIGHT);
    Element_setPivot(p, PART_TOP_LEFT);
    r = Element_resolve(p, parent);
    CHECK(closef(r.x, 300) && closef(r.y, 150));     // hangs off the corner

    Element_setAnchor(p, PART_TOP_CENTER);
    Element_setPivot(p, PART_BOTTOM_CENTER);
    r = Element_resolve(p, parent);
    CHECK(closef(r.x, 190) && closef(r.y, 40));

    // offset shifts the result by exactly the offset
    Element_setAnchor(p, PART_TOP_LEFT);
    Element_setPivot(p, PART_TOP_LEFT);
    Element_setOffset(p, 5, 7);
    r = Element_resolve(p, parent);
    CHECK(closef(r.x, 105) && closef(r.y, 57));
    CHECK(closef(r.w, 20) && closef(r.h, 10));

    // --- ALL 81 anchor x pivot combos: the panel's pivot lands on the anchor ---
    Element_setOffset(p, 0, 0);
    for (int a = 0; a < PART_COUNT; a++) {
        for (int pv = 0; pv < PART_COUNT; pv++) {
            Element_setAnchor(p, a);
            Element_setPivot(p, pv);
            Rect rr = Element_resolve(p, parent);
            Point anchor = Part_point(parent, a);
            Point pivot = Part_point(rr, pv);
            if (!(closef(anchor.x, pivot.x) && closef(anchor.y, pivot.y))) {
                printf("FAIL a=%d p=%d: anchor(%.1f,%.1f) != pivot(%.1f,%.1f)\n",
                       a, pv, anchor.x, anchor.y, pivot.x, pivot.y);
                g_fail++;
            }
        }
    }

    // --- style + paint ---
    Element_setAnchor(p, PART_CENTER);
    Element_setPivot(p, PART_CENTER);
    Element_setBackground(p, COLOR_RGBA(1, 2, 3, 4));
    Element_setBorder(p, COLOR_RGBA(5, 6, 7, 8), 2);
    Element_setRadius(p, 8);
    Element_setShadowColor(p, COLOR_RGBA(0, 0, 0, 128));
    Element_setShadow(p, 3, 4, 2);
    CHECK(Element_radius(p) == 8.0f);
    CHECK(Element_anchor(p) == PART_CENTER);

    DisplayList *dl = DisplayList_0();
    Element_paint(p, (Rect){10, 10, 40, 20}, dl);
    CHECK(DisplayList_count(dl) == 2);               // shadow + body

    Element_setShadowColor(p, COLOR_CLEAR);
    DisplayList_clear(dl);
    Element_paint(p, (Rect){10, 10, 40, 20}, dl);
    CHECK(DisplayList_count(dl) == 1);               // no shadow

    // the painted body carries the radius + border
    const DrawCmd *body = DisplayList_cmds(dl);
    CHECK(body[0].radius == 8.0f);
    CHECK(body[0].border == 2.0f);
    CHECK(body[0].color == COLOR_RGBA(1, 2, 3, 4));
    CHECK(body[0].borderColor == COLOR_RGBA(5, 6, 7, 8));

    // radius clamps negative; invalid parts are ignored
    Element_setRadius(p, -5);
    CHECK(Element_radius(p) == 0.0f);
    Element_setAnchor(p, PART_TOP_LEFT);
    Element_setAnchor(p, 99);
    CHECK(Element_anchor(p) == PART_TOP_LEFT);
    Element_setPivot(p, PART_CENTER);
    Element_setPivot(p, -1);
    CHECK(Element_pivot(p) == PART_CENTER);

    // null-safe
    CHECK(!Element_isValid(nullptr));
    CHECK(Element_width(nullptr) == 0.0f);
    CHECK(Element_resolve(nullptr, parent).w == 0.0f);
    Element_paint(nullptr, (Rect){0, 0, 1, 1}, dl);       // must not crash

    DisplayList_free(dl);
    Element_destroy(p);
    Element_destroy(nullptr);

    if (g_fail == 0) printf("element_test: ALL PASS (9 anchors x 9 pivots = 81 combos)\n");
    return g_fail == 0 ? 0 : 1;
}
