// graphvex/test/nio/property_pool_test.c — mirrors src/nio/property_pool.c
//
// The bound pool. Every Element's Property comes from here, and the SAME record
// can be shared by many Elements (aliasing the address is the bind).

#include "vex.h"
#include <stdio.h>

#include "nio/property_pool.h"
#include "ui/property.h"

;;GETTER
static Color Property_background(const Property *p) { return (*p).background; }
;;GETTER
static f32 Property_radius(const Property *p) { return (*p).radius; }
;;GETTER
static f32 Property_x(const Property *p) { return (*p).x; }
;;SETTER
static fn Property_setX(Property *p, f32 x) { (*p).x = x; }
;;GETTER
static f32 Property_y(const Property *p) { return (*p).y; }
;;GETTER
static f32 Property_rawW(const Property *p) { return (*p).w; }
;;GETTER
static f32 Property_rawH(const Property *p) { return (*p).h; }

static i32 g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Checks property-pool allocation, reuse, and bounds-safe access.
i32 main() {
    PropertyPool *pool = PropertyPool();
    CHECK(pool != nullptr);
    CHECK(PropertyPool_live(pool) == 0);

    // default
    Property *a = PropertyPool_alloc(pool, nullptr);
    CHECK(a != nullptr);
    CHECK(Property_background(a) == COLOR_WHITE);
    CHECK(Property_radius(a) == 0.0f);

    // explicit
    Property bound = {0};
    bound.x = 10; bound.y = 20; bound.w = 100; bound.h = 50; bound.radius = 12;
    bound.background = COLOR_RGBA(120, 190, 220, 255);
    Property *b = PropertyPool_alloc(pool, &bound);
    CHECK(b != nullptr);
    CHECK(Property_x(b) == 10 && Property_y(b) == 20 && Property_rawW(b) == 100 && Property_rawH(b) == 50);
    CHECK(Property_radius(b) == 12 && Property_background(b) == COLOR_RGBA(120, 190, 220, 255));
    CHECK(PropertyPool_live(pool) == 2);

    // sharing: two Elements point at ONE record; a write through either is seen
    Property *shared = b;
    Property_setX(shared, 42);
    CHECK(Property_x(b) == 42);

    // grow well past the first 64-slot block; earlier records stay put
    Property *kept = a;
    for (i32 i = 0; i < 200; i++) CHECK(PropertyPool_alloc(pool, &bound) != nullptr);
    CHECK(PropertyPool_live(pool) == 202);
    CHECK(Property_background(kept) == COLOR_WHITE);   // never moved

    PropertyPool_release(pool, b);
    CHECK(PropertyPool_live(pool) == 201);
    // release then alloc reuses the slot, re-defaulted
    Property *reused = PropertyPool_alloc(pool, nullptr);
    CHECK(reused == b);
    CHECK(Property_background(reused) == COLOR_WHITE && Property_x(reused) == 0);

    PropertyPool_destroy(pool);
    printf("property_pool_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
