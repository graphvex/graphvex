#include "vex.h"
#include "vio/vio.h"
#include "nio/graphvex_memory.h"
#include <assert.h>

static fn testPool(VideoPool *pool, const utf8 *label) {
    assert(pool != nullptr);
    printf("[vio_test] backend=%s uma=%s slabBytes=%zu\n",
           VideoPool_driverName(pool),
           VideoMemory_isUma(pool) ? "true" : "false",
           VideoPool_slabBytes(pool));

    VideoArena *shared = VideoArena_create(pool, VIDEO_MEMORY_SHARED, 0u);
    assert(shared != nullptr);

    vpointer a = VideoArena_alloc(shared, 1024u, 256u);
    vpointer b = VideoArena_alloc(shared, 512u, 64u);
    assert(a != 0u && b != 0u && a != b);
    assert((a & 255u) == 0u);
    assert((b & 63u) == 0u);
    assert(VideoMemory_contains(pool, a, 1024u));
    assert(VideoMemory_contains(pool, b, 512u));

    pointer cpuA = VideoMemory_toPointer(pool, a);
    assert(cpuA != 0u);
    assert(VideoMemory_fromPointer(pool, cpuA) == a);
    assert(VideoMemory_map(pool, a) == (any) cpuA);

    u32 *words = (u32*) cpuA;
    for (u32 i = 0u; i < 64u; ++i)
        words[i] = 0xCAFE0000u | i;

    u32 readback[64] = {0};
    assert(VideoMemory_read(pool, readback, a, sizeof readback));
    for (u32 i = 0u; i < 64u; ++i)
        assert(readback[i] == (0xCAFE0000u | i));

    u32 payload[32] = {0};
    for (u32 i = 0u; i < 32u; ++i)
        payload[i] = 0xBEEF0000u | i;
    assert(VideoMemory_write(pool, b, payload, sizeof payload));
    u32 *cpuB = (u32*) VideoMemory_map(pool, b);
    assert(cpuB != nullptr);
    for (u32 i = 0u; i < 32u; ++i)
        assert(cpuB[i] == (0xBEEF0000u | i));

    VideoArena_free(shared, b);
    vpointer c = VideoArena_alloc(shared, 256u, 64u);
    assert(c == b);

    VideoArena_reset(shared);
    assert(VideoArena_usedBytes(shared) == 0u);
    vpointer d = VideoArena_alloc(shared, 256u, 256u);
    assert(d == a);

    VideoArena *capped = VideoArena_create(pool, VIDEO_MEMORY_SHARED, 512u);
    assert(capped != nullptr);
    assert(VideoArena_alloc(capped, 512u, 32u) != 0u);
    assert(VideoArena_alloc(capped, 32u, 32u) == 0u);
    assert(VideoArena_exhaustionCount(capped) == 1u);
    VideoArena_destroy(capped);

    VideoArena *priv = VideoArena_create(pool, VIDEO_MEMORY_PRIVATE, 0u);
    assert(priv != nullptr);
    vpointer pv = VideoArena_alloc(priv, 1024u, 256u);
    assert(pv != 0u);
    assert(VideoMemory_toPointer(pool, pv) == 0u);
    assert(VideoMemory_map(pool, pv) == nullptr);
    u32 privIn[32] = {0};
    u32 privOut[32] = {0};
    for (u32 i = 0u; i < 32u; ++i)
        privIn[i] = 0x12340000u | i;
    assert(VideoMemory_write(pool, pv, privIn, sizeof privIn));
    assert(VideoMemory_read(pool, privOut, pv, sizeof privOut));
    for (u32 i = 0u; i < 32u; ++i)
        assert(privOut[i] == (0x12340000u | i));
    VideoArena_destroy(priv);

    VideoArena_destroy(shared);
    printf("[vio_test] %s: PASS\n", label);
}

i32 main() {
    VideoPool *sw = VideoPool_createSoftware(64u * 1024u);
    testPool(sw, "software-uma");
    VideoPool_destroy(sw);

#if defined(VEX_PLATFORM_APPLE)
    VideoPool *mtl = VideoPool_createMetal(64u * 1024u);
    if (mtl != nullptr) {
        testPool(mtl, "metal");
        VideoPool_destroy(mtl);
    } else {
        puts("[vio_test] metal: SKIP (no Metal device exposed in this sandbox)");
    }
#endif

    Graphvex_arenaShutdown();
    puts("vio_test: ALL PASS");
    return 0;
}
