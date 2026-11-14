// graphvex/test/element_property_test.c — mirrors src/ui/element.c
//
// Element's rectangle + style live in a pooled, SHAREABLE Property. Two Elements
// may point at one bound (a write is seen by both); owning detaches; revalidate
// clears only the dirty branches; a radius>0 parent clips its children.

#include "vex.h"
#include <stdio.h>

#include "graphics/graphics.h"
#include "graphvex/lang/image.h"
#include "nio/property_pool.h"
#include "ui/element.h"
#include "ui/property.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Returns the alpha channel at the requested image pixel.
static int alpha_at(const Image *img, int x, int y) {
    return Image_pixels(img)[(bytes)y * Image_stride(img) + (bytes)x * 4 + 3];
}

// Verifies element placement properties affect paint and hit behavior.
int main() {
    // 1. sharing a bound: two Elements, one Property
    Element *a = Element();
    Element_setSize(a, 100, 50);
    Element_setBackground(a, COLOR_RGBA(10, 20, 30, 255));

    Element *b = Element();
    Element_setProperty(b, Element_property(a));         // bind (borrow)
    CHECK(Element_property(b) == Element_property(a));
    CHECK((*Element_property(b)).background == COLOR_RGBA(10, 20, 30, 255));

    Element_setBackground(a, COLOR_RGBA(200, 100, 100, 255));
    CHECK((*Element_property(b)).background == COLOR_RGBA(200, 100, 100, 255));  // shared write

    Element_ownProperty(b);                              // detach into a private copy
    CHECK(Element_property(b) != Element_property(a));
    Element_setBackground(a, COLOR_RGBA(1, 2, 3, 255));
    CHECK((*Element_property(b)).background == COLOR_RGBA(200, 100, 100, 255));  // no longer follows

    // 2. revalidate + dirty subtree
    Element *root = Element();
    Element_setSize(root, 200, 200);
    Element_add(root, a);
    Element_revalidate(root);                            // settle
    CHECK(!Element_isDirty(root));
    Element_markDirty(a);
    CHECK(Element_isDirty(a) && Element_isDirty(root));  // marked up the chain
    Element_revalidate(root);
    CHECK(!Element_isDirty(root) && !Element_isDirty(a));

    // 3. a rounded parent clips its children (rectangular clip this slice)
    CHECK(Graphics_use(BACKEND_RASTER));
    CHECK(Graphics_resize(100, 100));
    Element *clipParent = Element();
    Element_setSize(clipParent, 60, 60);
    Element_setRadius(clipParent, 30);                   // also the clip trigger
    Element_setBackground(clipParent, COLOR_CLEAR);      // transparent body
    Element *big = Element();
    Element_setSize(big, 200, 200);
    Element_setBackground(big, COLOR_RGBA(255, 0, 0, 255));
    Element_add(clipParent, big);

    DisplayList *dl = DisplayList_0();
    Element_paint(clipParent, (Rect){0, 0, 60, 60}, dl);
    CHECK(Graphics_begin());
    CHECK(Graphics_clear(COLOR_CLEAR));
    CHECK(Graphics_submit(dl));
    CHECK(Graphics_end());
    Image *img = Image_0();
    CHECK(Graphics_capture(img));
    CHECK(alpha_at(img, 50, 50) == 255);                 // inside the viewport: child shows
    CHECK(alpha_at(img, 30, 30) == 255);                 // the circle's centre
    CHECK(alpha_at(img, 80, 80) == 0);                   // beyond the rect: clipped away
    CHECK(alpha_at(img, 2, 2) == 0);                     // inside the rect but OUTSIDE the
                                                         // circle: the ROUNDED mask cuts it
    Image_destroy(img);
    DisplayList_free(dl);

    // 4. min/max size clamp MODULATES layout at read time
    Element *sized = Element();
    Element_setSize(sized, 300, 40);
    Element_setMinimumSize(sized, 80, 90);       // floor
    Element_setMaximumSize(sized, 200, 0);       // width ceiling; height unbounded
    CHECK(Element_width(sized) == 200.0f);       // 300 clamped down
    CHECK(Element_height(sized) == 90.0f);       // 40 clamped up
    Rect sr = Element_resolve(sized, (Rect){0, 0, 1000, 1000});
    CHECK(sr.w == 200.0f && sr.h == 90.0f);      // resolve uses the clamp

    // a floor set after the size still applies: the clamp is on the BOUND
    Element_setSize(sized, 10, 10);
    CHECK(Element_width(sized) == 80.0f);        // 10 -> floor 80
    Element_setMaximumSize(sized, 0, 0);         // clear ceilings
    Element_setMinimumSize(sized, 0, 0);         // clear floors
    CHECK(Element_width(sized) == 10.0f);

    // sharing: an aliasing element clamps identically through the shared bound
    Element *alias = Element();
    Element_setProperty(alias, Element_property(sized));   // borrow
    Element_setMaximumSize(sized, 60, 60);
    CHECK(Element_width(alias) == 10.0f);        // 10 < 60, unchanged
    Element_setSize(sized, 500, 500);
    CHECK(Element_width(alias) == 60.0f);        // ceiling seen via the alias
    CHECK(Element_height(alias) == 60.0f);
    Element_destroy(alias);
    Element_destroy(sized);

    Element_destroy(b);
    Element_destroy(clipParent);   // frees big
    Element_destroy(root);         // frees a

    if (g_fail == 0) printf("element_property_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
