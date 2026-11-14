// graphvex/test/nio/property_pool_test.c — mirrors src/nio/property_pool.c
//
// The bound pool. Every Element's Property comes from here, and the SAME record
// can be shared by many Elements (aliasing the address is the bind).

#include "vex.h"
#include <stdio.h>

#include "nio/property_pool.h"
#include "ui/property.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Checks property-pool allocation, reuse, and bounds-safe access.
int main() {
    PropertyPool *pool = PropertyPool();
    CHECK(pool != nullptr);
    CHECK(PropertyPool_live(pool) == 0);

    // default
    Property *a = PropertyPool_alloc(pool, nullptr);
    CHECK(a != nullptr);
    CHECK((*a).background == COLOR_WHITE);
    CHECK((*a).radius == 0.0f);

    // explicit
    Property bound = {0};
    bound.x = 10; bound.y = 20; bound.w = 100; bound.h = 50; bound.radius = 12;
    bound.background = COLOR_RGBA(120, 190, 220, 255);
    Property *b = PropertyPool_alloc(pool, &bound);
    CHECK(b != nullptr);
    CHECK((*b).x == 10 && (*b).y == 20 && (*b).w == 100 && (*b).h == 50);
    CHECK((*b).radius == 12 && (*b).background == COLOR_RGBA(120, 190, 220, 255));
    CHECK(PropertyPool_live(pool) == 2);

    // sharing: two Elements point at ONE record; a write through either is seen
    Property *shared = b;
    (*shared).x = 42;
    CHECK((*b).x == 42);

    // grow well past the first 64-slot block; earlier records stay put
    Property *kept = a;
    for (int i = 0; i < 200; i++) CHECK(PropertyPool_alloc(pool, &bound) != nullptr);
    CHECK(PropertyPool_live(pool) == 202);
    CHECK((*kept).background == COLOR_WHITE);   // never moved

    PropertyPool_release(pool, b);
    CHECK(PropertyPool_live(pool) == 201);
    // release then alloc reuses the slot, re-defaulted
    Property *reused = PropertyPool_alloc(pool, nullptr);
    CHECK(reused == b);
    CHECK((*reused).background == COLOR_WHITE && (*reused).x == 0);

    PropertyPool_destroy(pool);
    printf("property_pool_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
