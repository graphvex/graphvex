/**
 * Consolidated automated owner test suite for vex/graphvex/api.
 * Usage: api_test [core|vio]
 */

#include "graphvex.h"
#include "vio/vio.h"
#include "nio/graphvex_memory.h"
#include "tests/api_test.h"
#include <assert.h>
#include <string.h>

static i32 g_failures = 0;

/** Records a failed check with a human-readable diagnostic. */
static fn api_check(i32 ok, const utf8 *what) {
    if (ok)
        return;
    fprintf(stderr, "api_test: FAIL %s\n", what);
    g_failures++;
}

/** Proves unified Graphics API, zero-copy crop, bindless slots, and shader routing. */
static fn suite_core() {
    u32 color = ColorRGBA_pack(0x12u, 0x34u, 0x56u, 0x78u);
    api_check(color == 0x12345678u, "ColorRGBA_pack packs 0xRRGGBBAA");
    api_check(ColorRGBA_red(color) == 0x12u && ColorRGBA_green(color) == 0x34u &&
              ColorRGBA_blue(color) == 0x56u && ColorRGBA_alpha(color) == 0x78u,
              "ColorRGBA unpacks monotonic R,G,B,A");
    api_check(COLOR_BLACK_RGBA == 0x000000FFu, "Opaque black is 0x000000FF");

    VideoPool *pool = VideoPool_createSoftware(4u * 1024u * 1024u);
    api_check(pool != nullptr, "VideoPool_createSoftware succeeds");
    VideoArena *arena = VideoArena(pool, VIDEO_MEMORY_SHARED, 0u);
    api_check(arena != nullptr, "VideoArena succeeds");
    Graphics *g = Graphics(pool);
    api_check(g != nullptr, "Graphics succeeds");

    GraphicsImage canvas = Graphics_createImage(g, arena, 16u, 16u, PIXEL_FORMAT_RGBA8_UNORM);
    api_check(GraphicsImage_isValid(&canvas), "Graphics_createImage produces valid vpointer image");
    api_check(GraphicsImage_bindlessSlot(&canvas) == 1u, "First image auto-registered at bindless slot 1");
    api_check(!GraphicsImage_isSubview(&canvas), "Fresh image is not a subview");

    GraphicsImage wide = Graphics_createImage(g, arena, 256u, 128u, PIXEL_FORMAT_RGBA8_UNORM);
    api_check(GraphicsImage_bindlessSlot(&wide) == 2u, "Second image gets bindless slot 2");
    f32 fx = 0.0f;
    f32 fy = 0.0f;
    f32 fw = 0.0f;
    f32 fh = 0.0f;
    GraphicsImage_fitRect(&wide, 0.0f, 0.0f, 100.0f, 100.0f, &fx, &fy, &fw, &fh);
    api_check(fw == 100.0f && fh == 50.0f && fy == 25.0f, "256x128 image fits centered in 100x100");

    Graphics_setTarget(g, &canvas);
    api_check(Graphics_clear(g, 0x102030FFu), "Full clear queued");
    Graphics_setScissor(g, 4, 4, 12, 12);
    api_check(Graphics_clear(g, 0xFF00AAFFu), "Scissor clear queued");
    Graphics_resetScissor(g);
    api_check(Graphics_submit(g), "Graphics_submit executes clears");
    api_check(Graphics_frameIndex(g) == 1u, "Frame index advances on submit");

    u8 *px = (u8*) VideoMemory_map(pool, GraphicsImage_data(&canvas));
    api_check(px != nullptr, "UMA map returns direct CPU pointer to canvas");
    api_check(px[0] == 0x10u && px[1] == 0x20u && px[2] == 0x30u && px[3] == 0xFFu,
              "Outer pixel keeps base clear color");
    bytes centerOff = (bytes) 4u * (bytes) GraphicsImage_strideBytes(&canvas) + 4u * 4u;
    api_check(px[centerOff] == 0xFFu && px[centerOff + 1u] == 0x00u &&
              px[centerOff + 2u] == 0xAAu && px[centerOff + 3u] == 0xFFu,
              "Scissored inner pixel has second clear color");

    GraphicsImage view = Graphics_crop(&canvas, 4, 4, 12, 12);
    api_check(GraphicsImage_isValid(&view) && GraphicsImage_isSubview(&view),
              "Graphics_crop produces valid zero-copy subview");
    api_check(GraphicsImage_width(&view) == 8u && GraphicsImage_height(&view) == 8u,
              "Cropped view has 8x8 extent");
    api_check(GraphicsImage_strideBytes(&view) == GraphicsImage_strideBytes(&canvas),
              "Cropped view preserves parent strideBytes");
    api_check(GraphicsImage_data(&view) == GraphicsImage_data(&canvas) + centerOff,
              "Cropped view vpointer points directly to (4,4) with zero copy");

    GraphicsImage fullCopy = Graphics_copy(g, arena, &canvas);
    GraphicsImage cropCopy = Graphics_copy(g, arena, &canvas, 4, 4, 12, 12);
    api_check(Graphics_submit(g), "Graphics_submit executes copies");
    api_check(GraphicsImage_isValid(&fullCopy) && !GraphicsImage_isSubview(&fullCopy),
              "Graphics_copy(g, arena, src) allocates standalone tight image");
    api_check(GraphicsImage_width(&cropCopy) == 8u && GraphicsImage_height(&cropCopy) == 8u &&
              GraphicsImage_strideBytes(&cropCopy) == 8u * 4u,
              "Graphics_copy(g, arena, src, x0, y0, x1, y1) packs tightly");
    u8 *cropPx = (u8*) VideoMemory_map(pool, GraphicsImage_data(&cropCopy));
    api_check(cropPx != nullptr && cropPx[0] == 0xFFu && cropPx[1] == 0x00u &&
              cropPx[2] == 0xAAu && cropPx[3] == 0xFFu,
              "Cropped copy pixel (0,0) matches canvas (4,4)");

    GraphicsImage dst = Graphics_createImage(g, arena, 16u, 16u, PIXEL_FORMAT_RGBA8_UNORM);
    Graphics_setTarget(g, &dst);
    api_check(Graphics_clear(g, COLOR_BLACK_RGBA), "Clear dst to black");
    Graphics_setScissor(g, 0, 0, 4, 4);
    api_check(Graphics_copyTo(g, &dst, 0, 0, &cropCopy), "Scissored copyTo queued");
    Graphics_resetScissor(g);
    api_check(Graphics_submit(g), "Submit scissored copyTo");
    u8 *dstPx = (u8*) VideoMemory_map(pool, GraphicsImage_data(&dst));
    api_check(dstPx[0] == 0xFFu && dstPx[2] == 0xAAu, "Copied pixel inside scissor");
    api_check(dstPx[5u * 4u] == 0x00u && dstPx[5u * 4u + 3u] == 0xFFu,
              "Pixel outside scissor untouched");

    u32 blurShader = Graphics_registerShader(g, "collage/blur_smol", SHADER_STAGE_COMPUTE);
    u32 meshShader = Graphics_registerShader(g, "meld/mesh", SHADER_STAGE_VERTEX);
    api_check(blurShader == 1u && meshShader == 2u, "Shaders register without vendor handles");
    vpointer dummyVerts = VideoArena_alloc(arena, 256u, 32u);
    api_check(Graphics_dispatch(g, blurShader, 8u, 8u, 1u, 0u, 0u), "Graphics_dispatch queues compute");
    api_check(Graphics_draw(g, meshShader, dummyVerts, 0u, 3u, 0u, 0u), "Graphics_draw queues draw");
    api_check(Graphics_present(g, nullptr, &canvas), "Graphics_present flushes and presents with no swapchain");

    ShaderSource mslOnly = { 0 };
    ShaderSource_setModuleName(&mslOnly, "metal_custom");
    ShaderSource_setAvailableLanguageMask(&mslOnly, 1u << SHADER_LANG_MSL);
    api_check(ShaderSource_supportsLanguage(&mslOnly, SHADER_LANG_MSL), "MSL-only supports MSL");
    api_check(!ShaderSource_supportsLanguage(&mslOnly, SHADER_LANG_GLSL), "MSL-only rejected on GLSL/SPIR-V");

    ShaderSource dual = { 0 };
    ShaderSource_setModuleName(&dual, "collage/blur");
    ShaderSource_setAvailableLanguageMask(&dual, (1u << SHADER_LANG_GLSL) | (1u << SHADER_LANG_WGSL));
    api_check(ShaderSource_supportsLanguage(&dual, SHADER_LANG_GLSL) &&
              ShaderSource_supportsLanguage(&dual, SHADER_LANG_MSL) &&
              ShaderSource_supportsLanguage(&dual, SHADER_LANG_WGSL),
              "GLSL+WGSL module supports all languages");
    api_check(ShaderSource_preferredLanguage(&dual, SHADER_LANG_WGSL) == SHADER_LANG_WGSL,
              "Prefers native WGSL when available");

    ShaderBytes mslFile = Shader_loadFile("vex/graphvex/api/shader/msl/builtin.metal");
    api_check(ShaderBytes_isValid(&mslFile) && ShaderBytes_length(&mslFile) > 100u,
              "Shader_loadFile reads shader/msl/builtin.metal via re/io/file");
    ShaderBytes_destroy(&mslFile);
    api_check(!ShaderBytes_isValid(&mslFile), "ShaderBytes_destroy resets handle");

    Graphics_destroy(g);
    VideoArena_destroy(arena);
    VideoPool_destroy(pool);
}

/** Exercises VideoPool and VideoArena allocation, read/write, cap, and private memory. */
static fn vio_testPool(VideoPool *pool, const utf8 *label) {
    assert(pool != nullptr);
    printf("[vio_test] backend=%s uma=%s slabBytes=%zu\n",
           VideoPool_driverName(pool),
           VideoMemory_isUma(pool) ? "true" : "false",
           VideoPool_slabBytes(pool));

    VideoArena *shared = VideoArena(pool, VIDEO_MEMORY_SHARED, 0u);
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

    u32 readback[64] = { 0 };
    assert(VideoMemory_read(pool, readback, a, sizeof readback));
    for (u32 i = 0u; i < 64u; ++i)
        assert(readback[i] == (0xCAFE0000u | i));

    u32 payload[32] = { 0 };
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

    VideoArena *capped = VideoArena(pool, VIDEO_MEMORY_SHARED, 512u);
    assert(capped != nullptr);
    assert(VideoArena_alloc(capped, 512u, 32u) != 0u);
    assert(VideoArena_alloc(capped, 32u, 32u) == 0u);
    assert(VideoArena_exhaustionCount(capped) == 1u);
    VideoArena_destroy(capped);

    VideoArena *priv = VideoArena(pool, VIDEO_MEMORY_PRIVATE, 0u);
    assert(priv != nullptr);
    vpointer pv = VideoArena_alloc(priv, 1024u, 256u);
    assert(pv != 0u);
    assert(VideoMemory_toPointer(pool, pv) == 0u);
    assert(VideoMemory_map(pool, pv) == nullptr);
    u32 privIn[32] = { 0 };
    u32 privOut[32] = { 0 };
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

/** Proves software UMA and Metal VideoPool/VideoArena behavior. */
static fn suite_vio() {
    VideoPool *sw = VideoPool_createSoftware(64u * 1024u);
    vio_testPool(sw, "software-uma");
    VideoPool_destroy(sw);

#if defined(VEX_PLATFORM_APPLE)
    VideoPool *mtl = VideoPool_createMetal(64u * 1024u);
    if (mtl != nullptr) {
        vio_testPool(mtl, "metal");
        VideoPool_destroy(mtl);
    } else {
        puts("[vio_test] metal: SKIP (no Metal device exposed in this sandbox)");
    }
#endif

    Graphvex_arenaShutdown();
}

/** Runs all automated vex/graphvex/api suites or a single named suite passed as argv[1]. */
i32 main(i32 argc, utf8 **argv) {
    const SuiteEntry suites[] = {
        { "core", suite_core },
        { "vio", suite_vio },
    };
    const utf8 *filter = argc > 1 ? argv[1] : nullptr;
    bytes ran = 0;

    for (bytes i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (filter && strcmp(filter, suites[i].name) != 0)
            continue;
        suites[i].run();
        ran++;
    }

    if (filter && ran == 0) {
        fprintf(stderr, "api_test: unknown suite '%s'\n", filter);
        return 2;
    }
    if (g_failures != 0) {
        fprintf(stderr, "api_test: %d FAILURES\n", g_failures);
        return 1;
    }
    puts("api_test: PASS");
    return 0;
}
