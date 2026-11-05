// Mirrors src/ui/element.{h,c}: two-bound geometry seam, not full Element proof.
// Event geometry remains fixed while own soft edges/shadows and visible subtree
// paint enlarge conservative absolute bounds. Child clips do not clip own halos.
// Surface: eventBound/absoluteBound, compatibility resolve/bounds, hierarchy,
// shared Property edits, visibility, min/max, null/property-less and zero sizes.
// Finite acyclic owner-thread trees only; GPU pixels, ordered ElementFilter
// support, allocation failures and concurrent mutation are outside this seam.
#include <math.h>
#include <stdio.h>

#include "ui/element.h"

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; \
} } while (0)

// Compares rectangles component-wise with the test float tolerance.
static bool same(Rect a, Rect b) {
    return fabsf(a.x - b.x) < 0.001f && fabsf(a.y - b.y) < 0.001f &&
           fabsf(a.w - b.w) < 0.001f && fabsf(a.h - b.h) < 0.001f;
}

// Creates a test element with explicit size and position properties.
static Element *node(float w, float h, float x, float y) {
    ElementDesc desc = {.width = w, .height = h,
        .offsetX = x, .offsetY = y, .background = COLOR_WHITE};
    Element *e = Element(&desc);
    CHECK(e != nullptr);
    return e;
}

// Checks event and absolute bounds for placement and style changes.
static void placement_and_styles(void) {
    Rect parent = {100, 50, 200, 100};
    Element *e = node(20, 10, 5, -7);
    for (int a = 0; a < PART_COUNT; a++) {
        for (int p = 0; p < PART_COUNT; p++) {
            Element_setAnchor(e, a);
            Element_setPivot(e, p);
            Rect event = Element_eventBound(e, parent);
            Point anchor = Part_point(parent, a), pivot = Part_point(event, p);
            CHECK(fabsf(pivot.x - anchor.x - 5) < 0.001f);
            CHECK(fabsf(pivot.y - anchor.y + 7) < 0.001f);
            Element_setBlur(e, 2);
            Element_setShadow(e, -8, 6, 3);
            CHECK(same(Element_eventBound(e, parent), event));
            CHECK(same(Element_resolve(e, parent), event));
            CHECK(same(Element_absoluteBound(e, parent),
                       (Rect){event.x - 11, event.y - 2, 33, 21}));
            CHECK(same(Element_bounds(e, parent), Element_absoluteBound(e, parent)));
            Element_setBlur(e, 0);
            Element_setShadowColor(e, COLOR_CLEAR);
        }
    }
    Element_setMinimumSize(e, 30, 12);
    CHECK(Element_eventBound(e, parent).w == 30);
    Element_setMinimumSize(e, 0, 0);
    Element_setMaximumSize(e, 15, 8);
    CHECK(Element_eventBound(e, parent).w == 15);
    CHECK(Element_eventBound(e, parent).h == 8);
    Element_destroy(e);
}

// Verifies descendant paint bounds honor clipping and subtree changes.
static void descendants_and_clips(void) {
    Rect parent = {100, 50, 400, 300};
    Element *root = node(40, 30, 0, 0);
    Element *child = node(10, 10, 60, -20);
    Element *grandchild = node(5, 5, 30, -10);
    Element_add(root, child);
    Element_add(child, grandchild);
    CHECK(same(Element_absoluteBound(root, parent), (Rect){100, 20, 95, 60}));
    CHECK(same(Element_eventBound(root, parent), (Rect){100, 50, 40, 30}));
    Element_setVisible(grandchild, false);
    CHECK(same(Element_absoluteBound(root, parent), (Rect){100, 30, 70, 50}));
    Element_setVisible(grandchild, true);
    Element_setClip(child, true);
    CHECK(same(Element_absoluteBound(root, parent), (Rect){100, 30, 70, 50}));
    Element_setClip(child, false);
    Element_setShadow(root, -4, 3, 2);
    Element_setClip(root, true);
    CHECK(same(Element_absoluteBound(root, parent), (Rect){94, 50, 46, 35}));
    Element_setClip(root, false);
    Element_setRadius(root, 8); // radius alone retains default child clipping
    CHECK(same(Element_absoluteBound(root, parent), (Rect){94, 50, 46, 35}));
    Element_setRadius(root, 0);
    CHECK(same(Element_absoluteBound(root, parent), (Rect){94, 20, 101, 65}));
    Element_setVisible(root, false);
    CHECK(same(Element_absoluteBound(root, parent), (Rect){0}));
    CHECK(same(Element_eventBound(root, parent), (Rect){100, 50, 40, 30}));
    Element_setVisible(root, true);
    CHECK(Element_remove(child));
    CHECK(same(Element_absoluteBound(root, parent), (Rect){94, 50, 46, 35}));
    Element_destroy(child);
    Element_destroy(root);
}

// Checks effect-expanded paint bounds do not expand event hit bounds.
static void halo_and_hit(void) {
    Element *root = node(40, 30, 0, 0);
    Element *child = node(10, 10, 35, 10);
    Element_add(root, child);
    Element_setShadow(child, 10, 0, 2);
    CHECK(same(Element_absoluteBound(root, (Rect){0}), (Rect){0, 0, 57, 30}));
    CHECK(Element_hit(root, 50, 15) == nullptr); // painted halo is not an event region
    CHECK(Element_hit(root, 43, 15) == child);
    Element_setClip(root, true);
    CHECK(same(Element_absoluteBound(root, (Rect){0}), (Rect){0, 0, 40, 30}));
    CHECK(Element_hit(root, 43, 15) == nullptr);
    Element_setClip(root, false);
    Element_setRadius(root, 8);
    CHECK(Element_hit(root, 0, 0) == nullptr);
    CHECK(same(Element_absoluteBound(root, (Rect){0}), (Rect){0, 0, 40, 30}));
    Element_destroy(root);
}

// Tests empty trees, shared properties, and null-safe bound queries.
static void empty_and_shared(void) {
    Rect parent = {10, 20, 50, 50};
    CHECK(same(Element_eventBound(nullptr, parent), (Rect){0}));
    CHECK(same(Element_absoluteBound(nullptr, parent), (Rect){0}));
    CHECK(same(Element_bounds(nullptr, parent), (Rect){0}));
    Element *root = node(0, 0, 0, 0);
    Element *child = node(5, 5, 20, 10);
    Element_add(root, child);
    CHECK(same(Element_absoluteBound(root, parent), (Rect){30, 30, 5, 5}));
    Element_setClip(root, true);
    CHECK(Rect_isEmpty(Element_absoluteBound(root, parent)));
    Element_setClip(root, false);
    Property shared = {.w = 7, .h = 9, .background = COLOR_CLEAR};
    Element_setProperty(root, &shared);
    shared.w = 8; // borrowed record observed without a stale bounds cache
    CHECK(same(Element_eventBound(root, parent), (Rect){10, 20, 8, 9}));
    CHECK(same(Element_absoluteBound(root, parent), (Rect){10, 20, 25, 15}));
    Element_setProperty(root, nullptr); // paint also stops the property-less subtree
    CHECK(same(Element_absoluteBound(root, parent), (Rect){0}));
    CHECK(same(Element_eventBound(root, parent), (Rect){0}));
    Element_destroy(root);
    CHECK(shared.w == 8 && shared.h == 9);
}

// Runs element bound placement, descendant, halo, and sharing scenarios.
int main(void) {
    placement_and_styles();
    descendants_and_clips();
    halo_and_hit();
    empty_and_shared();
    if (!failures) puts("element_bounds_test: PASS (headless two-bound geometry)");
    return failures ? 1 : 0;
}
