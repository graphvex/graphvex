// graphvex/test/nio/pool_test.c — mirrors src/nio/pool.c
//
// The whole reason a pool exists here: STABLE ADDRESSES. Growing past a block
// must not move a live item, because an Element may hold the pointer for the
// pool's life and share it with others.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "nio/pool.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Tests pool growth, stable item storage, release, and rejected operations.
int main() {
    Pool *p = Pool_new(32, 4);       // 4 slots per block -> 10 allocs force growth
    CHECK(p != nullptr);
    CHECK(Pool_stride(p) == 32);
    CHECK(Pool_live(p) == 0);

    any items[10];
    for (int i = 0; i < 10; i++) {
        items[i] = Pool_alloc(p);
        CHECK(items[i] != nullptr);
        memset(items[i], 0xAB, 32);
    }
    CHECK(Pool_live(p) == 10);

    // STABLE: block growth never moved the first slot
    for (int i = 0; i < 10; i++) {
        const u8 *b = items[i];
        CHECK(b[0] == 0xAB && b[31] == 0xAB);
    }
    // every slot is distinct and contained
    for (int i = 0; i < 10; i++) {
        CHECK(Pool_contains(p, items[i]));
        for (int j = i + 1; j < 10; j++) CHECK(items[i] != items[j]);
    }

    // release is LIFO-reused and re-zeroed
    Pool_release(p, items[3]);
    CHECK(Pool_live(p) == 9);
    any reused = Pool_alloc(p);
    CHECK(reused == items[3]);
    CHECK(((u8*) reused)[0] == 0);
    CHECK(Pool_live(p) == 10);

    // a foreign pointer is not ours
    int local = 0;
    CHECK(!Pool_contains(p, &local));

    // null-safety
    CHECK(Pool_alloc(nullptr) == nullptr);
    CHECK(!Pool_contains(nullptr, items[0]));
    CHECK(Pool_live(nullptr) == 0);
    Pool_release(nullptr, nullptr);
    Pool_destroy(nullptr);

    Pool_destroy(p);
    printf("pool_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
