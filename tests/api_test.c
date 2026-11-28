#include "re/primitive/vex-string.h"
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

static fn api_check(i32 ok, string what)
{
    if (ok)
        return;

    fprint(stderr, "api_test: FAIL ", what);
    g_failures++;
}

static fn suite_core()
{
    u32 color = ColorRGBA_pack(0x12u, 0x34u, 0x56u, 0x78u);
    api_check(color == 0x12345678u, "ColorRGBA_pack packs 0xRRGGBBAA");
    api_check(ColorRGBA_red(color) == 0x12u && ColorRGBA_green(color) == 0x34u &&
              ColorRGBA_blue(color) == 0x56u && ColorRGBA_alpha(color) == 0x78u,
              "ColorRGBA unpacks monotonic R,G,B,A");
    api_check(COLOR_BLACK_RGBA == 0x000000FFu, "Opaque black is 0x000000FF");

    ImagePool *pool = ImagePool_software(4u * 1024u * 1024u);
    api_check(pool != nullptr, "ImagePool_software succeeds");
    ImageArena *arena = ImageArena(pool, IMAGE_MEMORY_SHARED, 0u);
    api_check(arena != nullptr, "ImageArena succeeds");
    Graphics *g = Graphics(pool);
    api_check(g != nullptr, "Graphics succeeds");

    GraphicsImage canvas = GraphicsImage(g, arena, 16u, 16u, PIXEL_FORMAT_RGBA8_UNORM);
    api_check(GraphicsImage_isValid(&canvas), "GraphicsImage produces valid vpointer image");
    api_check(Image_isGraphics(GraphicsImage_imageConst(&canvas)), "GraphicsImage is IMAGE_KIND_GRAPHICS");
    api_check(!Image_isBacking(GraphicsImage_imageConst(&canvas)), "GraphicsImage is not backing");
    api_check(Image_asGraphicsConst(GraphicsImage_imageConst(&canvas)) == &canvas, "Image_asGraphicsConst round-trips");
    api_check(Image_asBackingConst(GraphicsImage_imageConst(&canvas)) == nullptr, "Image_asBackingConst rejects GraphicsImage");
    api_check(GraphicsImage_bindlessSlot(&canvas) == 1u, "First image auto-registered at bindless slot 1");
    api_check(!GraphicsImage_isSubview(&canvas), "Fresh image is not a subview");

    GraphicsBackingImage backing = GraphicsBackingImage(g, 32u, 16u, PIXEL_FORMAT_NV12);
    api_check(GraphicsBackingImage_isValid(&backing), "GraphicsBackingImage allocates valid backing");
    api_check(Image_isBacking(GraphicsBackingImage_imageConst(&backing)), "GraphicsBackingImage is IMAGE_KIND_BACKING");
    api_check(!Image_isGraphics(GraphicsBackingImage_imageConst(&backing)), "GraphicsBackingImage is not GraphicsImage");
    api_check(Image_asGraphicsConst(GraphicsBackingImage_imageConst(&backing)) == nullptr, "Image_asGraphicsConst rejects GraphicsBackingImage");
    api_check(Image_asBackingConst(GraphicsBackingImage_imageConst(&backing)) == &backing, "Image_asBackingConst round-trips");
    api_check(GraphicsBackingImage_planeCount(&backing) == 2u, "NV12 backing has 2 planes");
    api_check(GraphicsBackingImage_setPurgeable(&backing, true), "Backing setPurgeable(true) succeeds");
    api_check(GraphicsBackingImage_isPurgeable(&backing), "Backing reports purgeable");
    api_check(GraphicsBackingImage_setPurgeable(&backing, false), "Backing setPurgeable(false) succeeds");
    GraphicsBackingImage_destroy(&backing);
    api_check(!GraphicsBackingImage_isValid(&backing), "Destroyed backing is invalid");

    Surface surf = Surface(g, nullptr, 320u, 240u);
    api_check(Surface_isValid(&surf), "Surface inherits valid GraphicsBackingImage");
    api_check(Image_isBacking(Surface_imageConst(&surf)), "Surface is IMAGE_KIND_BACKING");
    api_check(Surface_width(&surf) == 320u && Surface_height(&surf) == 240u, "Surface reports dimensions");
    Surface_deinit(&surf);

    GraphicsImage wide = GraphicsImage(g, arena, 256u, 128u, PIXEL_FORMAT_RGBA8_UNORM);
    f32 fx = 0.0f;
    f32 fy = 0.0f;
    f32 fw = 0.0f;
    f32 fh = 0.0f;
    Image_fitRect(GraphicsImage_imageConst(&wide), 0.0f, 0.0f, 100.0f, 100.0f, &fx, &fy, &fw, &fh);
    api_check(fw == 100.0f && fh == 50.0f && fy == 25.0f, "256x128 Image fits centered in 100x100");

    Graphics_setTarget(g, GraphicsImage_image(&canvas));
    api_check(Graphics_clear(g, 0x102030FFu), "Full clear queued");
    Graphics_setScissor(g, 4, 4, 12, 12);
    api_check(Graphics_clear(g, 0xFF00AAFFu), "Scissor clear queued");
    Graphics_resetScissor(g);
    api_check(Graphics_submit(g), "Graphics_submit executes clears");
    api_check(Graphics_frameIndex(g) == 1u, "Frame index advances on submit");

    u8 *px = (u8*) ImageMemory_map(pool, GraphicsImage_data(&canvas));
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

    GraphicsImage fullCopy = Graphics_copy(g, arena, GraphicsImage_imageConst(&canvas));
    GraphicsImage cropCopy = Graphics_copy(g, arena, &canvas, 4, 4, 12, 12);
    api_check(Graphics_submit(g), "Graphics_submit executes copies");
    api_check(GraphicsImage_isValid(&fullCopy) && !GraphicsImage_isSubview(&fullCopy),
              "Graphics_copy(g, arena, Image) allocates standalone tight image");
    api_check(GraphicsImage_width(&cropCopy) == 8u && GraphicsImage_height(&cropCopy) == 8u &&
              GraphicsImage_strideBytes(&cropCopy) == 8u * 4u,
              "Graphics_copy(g, arena, src, x0, y0, x1, y1) packs tightly");
    u8 *cropPx = (u8*) ImageMemory_map(pool, GraphicsImage_data(&cropCopy));
    api_check(cropPx != nullptr && cropPx[0] == 0xFFu && cropPx[1] == 0x00u &&
              cropPx[2] == 0xAAu && cropPx[3] == 0xFFu,
              "Cropped copy pixel (0,0) matches canvas (4,4)");

    ImagePool *auxPool = ImagePool_software(1024u * 1024u);
    ImageArena *auxArena = ImageArena(auxPool, IMAGE_MEMORY_SHARED, 0u);
    api_check(Graphics_attachPool(g, auxPool), "Graphics_attachPool registers sibling domain pool");
    GraphicsImage auxDst = GraphicsImage(g, auxArena, 8u, 8u, PIXEL_FORMAT_RGBA8_UNORM);
    api_check(Graphics_copyTo(g, &auxDst, 0, 0, GraphicsImage_imageConst(&cropCopy)),
              "Cross-pool copyTo queues cleanly");
    api_check(Graphics_submit(g), "Submit cross-pool copyTo");
    u8 *auxPx = (u8*) ImageMemory_map(auxPool, GraphicsImage_data(&auxDst));
    api_check(auxPx != nullptr && auxPx[0] == 0xFFu && auxPx[2] == 0xAAu,
              "Cross-pool copy matches source pixel");
    Graphics_detachPool(g, auxPool);
    ImageArena_destroy(auxArena);
    ImagePool_destroy(auxPool);

    GraphicsImage dst = GraphicsImage(g, arena, 16u, 16u, PIXEL_FORMAT_RGBA8_UNORM);
    Graphics_setTarget(g, GraphicsImage_image(&dst));
    api_check(Graphics_clear(g, COLOR_BLACK_RGBA), "Clear dst to black");
    Graphics_setScissor(g, 0, 0, 4, 4);
    api_check(Graphics_copyTo(g, &dst, 0, 0, GraphicsImage_imageConst(&cropCopy)), "Scissored copyTo queued");
    Graphics_resetScissor(g);
    api_check(Graphics_submit(g), "Submit scissored copyTo");
    u8 *dstPx = (u8*) ImageMemory_map(pool, GraphicsImage_data(&dst));
    api_check(dstPx[0] == 0xFFu && dstPx[2] == 0xAAu, "Copied pixel inside scissor");
    api_check(dstPx[5u * 4u] == 0x00u && dstPx[5u * 4u + 3u] == 0xFFu,
              "Pixel outside scissor untouched");

    u32 blurShader = Graphics_registerShader(g, "collage/blur_smol", SHADER_STAGE_COMPUTE);
    u32 meshShader = Graphics_registerShader(g, "meld/mesh", SHADER_STAGE_VERTEX);
    api_check(blurShader == 1u && meshShader == 2u, "Shaders register without vendor handles");
    vpointer dummyVerts = ImageArena_alloc(arena, 256u, 32u);
    api_check(Graphics_dispatch(g, blurShader, 8u, 8u, 1u, 0u, 0u), "Graphics_dispatch queues compute");
    api_check(Graphics_draw(g, meshShader, dummyVerts, 0u, 3u, 0u, 0u), "Graphics_draw queues draw");
    api_check(Graphics_present(g, nullptr, GraphicsImage_imageConst(&canvas)),
              "Graphics_present accepts Image with no swapchain");

    ShaderSource mslOnly;
    memset(&mslOnly, 0, sizeof(ShaderSource));
    ShaderSource_setModuleName(&mslOnly, "metal_custom");
    ShaderSource_setAvailableLanguageMask(&mslOnly, 1u << SHADER_LANG_MSL);
    api_check(ShaderSource_supportsLanguage(&mslOnly, SHADER_LANG_MSL), "MSL-only supports MSL");
    api_check(!ShaderSource_supportsLanguage(&mslOnly, SHADER_LANG_GLSL), "MSL-only rejected on GLSL/SPIR-V");

    ShaderSource dual;
    memset(&dual, 0, sizeof(ShaderSource));
    ShaderSource_setModuleName(&dual, "collage/blur");
    ShaderSource_setAvailableLanguageMask(&dual, (1u << SHADER_LANG_GLSL) | (1u << SHADER_LANG_WGSL));
    api_check(ShaderSource_supportsLanguage(&dual, SHADER_LANG_GLSL) &&
              ShaderSource_supportsLanguage(&dual, SHADER_LANG_MSL) &&
              ShaderSource_supportsLanguage(&dual, SHADER_LANG_WGSL),
              "GLSL+WGSL module supports all languages");
    api_check(ShaderSource_preferredLanguage(&dual, SHADER_LANG_WGSL) == SHADER_LANG_WGSL,
              "Prefers native WGSL when available");

    ShaderBytes mslFile = Shader_loadFile("vex/graphvex/api/metal/builtin.metal");
    api_check(ShaderBytes_isValid(&mslFile) && ShaderBytes_length(&mslFile) > 100u,
              "Shader_loadFile reads metal/builtin.metal via re/io/file");
    ShaderBytes_destroy(&mslFile);
    api_check(!ShaderBytes_isValid(&mslFile), "ShaderBytes_destroy resets handle");

    SecureMemoryArena *sa = Graphvex_secureArena();
    SecureHandle sh = SecureMemoryArena_alloc(sa, 0u, 64u);
    api_check(SecureHandle_isValid(sh), "SecureHandle allocates validly from sandcastles");
    u8 *sptr = (u8*) SecureMemoryArena_resolve(sa, sh);
    api_check(sptr != nullptr && sptr[0] == 0u && sptr[63] == 0u, "SecureMemoryArena zero-initializes on alloc");
    if (sptr) {
        sptr[0] = 0xABu;
        sptr[63] = 0xCDu;
    }
    SecureHandle stale = sh;
    api_check(SecureMemoryArena_free(sa, sh), "SecureMemoryArena_free releases handle");
    api_check(SecureMemoryArena_resolve(sa, stale) == nullptr,
              "SecureHandle invalidates after free");
    api_check(sptr != nullptr && sptr[0] == 0u && sptr[63] == 0u,
              "SecureMemoryArena scrubs payload to zero on free");

    Graphics_destroy(g);
    ImageArena_destroy(arena);
    ImagePool_destroy(pool);
}

static fn vio_testPool(ImagePool *pool, string label)
{
    assert(pool != nullptr);
    print("[vio_test] backend=", ImagePool_driverName(pool), " uma=", ImageMemory_isUma(pool) ? "true" : "false", " slabBytes=", ImagePool_slabBytes(pool));

    ImageArena *shared = ImageArena(pool, IMAGE_MEMORY_SHARED, 0u);
    assert(shared != nullptr);

    vpointer a = ImageArena_alloc(shared, 1024u, 256u);
    vpointer b = ImageArena_alloc(shared, 512u, 64u);
    assert(a != 0u && b != 0u && a != b);
    assert((a & 255u) == 0u);
    assert((b & 63u) == 0u);
    assert(ImageMemory_contains(pool, a, 1024u));
    assert(ImageMemory_contains(pool, b, 512u));

    pointer cpuA = ImageMemory_toPointer(pool, a);
    assert(cpuA != 0u);
    assert(ImageMemory_fromPointer(pool, cpuA) == a);
    assert(ImageMemory_map(pool, a) == (any) cpuA);

    u32 *words = (u32*) cpuA;
    for (u32 i = 0u; i < 64u; ++i)
        words[i] = 0xCAFE0000u | i;

    u32 readback[64];
    memset(readback, 0, sizeof(readback));
    assert(ImageMemory_read(pool, readback, a, sizeof readback));
    for (u32 i = 0u; i < 64u; ++i)
        assert(readback[i] == (0xCAFE0000u | i));

    u32 payload[32];
    memset(payload, 0, sizeof(payload));
    for (u32 i = 0u; i < 32u; ++i)
        payload[i] = 0xBEEF0000u | i;

    assert(ImageMemory_write(pool, b, payload, sizeof payload));
    u32 *cpuB = (u32*) ImageMemory_map(pool, b);
    assert(cpuB != nullptr);
    for (u32 i = 0u; i < 32u; ++i)
        assert(cpuB[i] == (0xBEEF0000u | i));

    ImageArena_free(shared, b);
    vpointer c = ImageArena_alloc(shared, 256u, 64u);
    assert(c == b);

    ImageArena_reset(shared);
    assert(ImageArena_usedBytes(shared) == 0u);
    vpointer d = ImageArena_alloc(shared, 256u, 256u);
    assert(d == a);

    ImageArena *capped = ImageArena(pool, IMAGE_MEMORY_SHARED, 512u);
    assert(capped != nullptr);
    assert(ImageArena_alloc(capped, 512u, 32u) != 0u);
    assert(ImageArena_alloc(capped, 32u, 32u) == 0u);
    assert(ImageArena_exhaustionCount(capped) == 1u);
    ImageArena_destroy(capped);

    ImageArena *priv = ImageArena(pool, IMAGE_MEMORY_PRIVATE, 0u);
    assert(priv != nullptr);
    vpointer pv = ImageArena_alloc(priv, 1024u, 256u);
    assert(pv != 0u);
    assert(ImageMemory_toPointer(pool, pv) == 0u);
    assert(ImageMemory_map(pool, pv) == nullptr);
    u32 privIn[32];
    u32 privOut[32];
    memset(privIn, 0, sizeof(privIn));
    memset(privOut, 0, sizeof(privOut));
    for (u32 i = 0u; i < 32u; ++i)
        privIn[i] = 0x12340000u | i;

    assert(ImageMemory_write(pool, pv, privIn, sizeof privIn));
    assert(ImageMemory_read(pool, privOut, pv, sizeof privOut));
    for (u32 i = 0u; i < 32u; ++i)
        assert(privOut[i] == (0x12340000u | i));

    ImageArena_destroy(priv);

    assert(IMAGE_MEMORY_DEFAULT_SLAB_BYTES == 1024u * 1024u);
    vpointer big = ImageArena_alloc(shared, 256u * 1024u, 256u);
    assert(big != 0u);
    bytes slabsBefore = ImagePool_slabCount(pool);
    ImageArena_trim(shared);
    bytes slabsAfter = ImagePool_slabCount(pool);
    assert(slabsAfter <= slabsBefore);
    vpointer smallAfter = ImageArena_alloc(shared, 4096u, 256u);
    assert(smallAfter != 0u);

    Graphics *gPurge = Graphics(pool);
    assert(gPurge != nullptr);
    Graphics_setSurfacePurgeable(gPurge, true);
    Graphics_setSurfacePurgeable(gPurge, false);

    GraphicsBackingImage bRgba = GraphicsBackingImage(gPurge, 64u, 32u, PIXEL_FORMAT_RGBA8_UNORM);
    assert(GraphicsBackingImage_isValid(&bRgba));
    assert(GraphicsBackingImage_planeCount(&bRgba) == 1u);
    assert(!GraphicsBackingImage_isInUse(&bRgba));
    assert(GraphicsBackingImage_setPurgeable(&bRgba, true));
    assert(GraphicsBackingImage_setPurgeable(&bRgba, false));

    GraphicsImage copyFromBacking = Graphics_copy(gPurge, shared, GraphicsBackingImage_imageConst(&bRgba));
    assert(GraphicsImage_isValid(&copyFromBacking));
    assert(Graphics_submit(gPurge));

    GraphicsBackingImage bNv12 = GraphicsBackingImage(gPurge, 64u, 32u, PIXEL_FORMAT_NV12);
    assert(GraphicsBackingImage_isValid(&bNv12));
    assert(GraphicsBackingImage_planeCount(&bNv12) == 2u);
    GraphicsImage copyNv12 = Graphics_copy(gPurge, shared, GraphicsBackingImage_imageConst(&bNv12));
    assert(GraphicsImage_isValid(&copyNv12));
    assert(Graphics_submit(gPurge));

    GraphicsBackingImage_destroy(&bNv12);
    GraphicsBackingImage_destroy(&bRgba);
    Graphics_destroy(gPurge);

    ImageArena_destroy(shared);
    print("[vio_test] ", label, ": PASS");
}

static fn suite_vio()
{
    ImagePool *sw = ImagePool_software(64u * 1024u);
    vio_testPool(sw, "software-uma");
    ImagePool_destroy(sw);

#if defined(VEX_PLATFORM_APPLE)
    ImagePool *mtl = ImagePool_metal(64u * 1024u);
    if (mtl != nullptr) {
        vio_testPool(mtl, "metal");
        ImagePool_destroy(mtl);
    } else {
        puts("[vio_test] metal: SKIP (no Metal device exposed in this sandbox)");
    }
#endif

    Graphvex_arenaShutdown();
}

i32 main(i32 argc, utf8 **argv)
{
    const SuiteEntry suites[] = {
        { "core", suite_core },
        { "vio", suite_vio },
    };
    string filter = argc > 1 ? argv[1] : nullptr;
    bytes ran = 0;

    for (bytes i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (filter && !string_equals(filter, suites[i].name))
            continue;

        suites[i].run();
        ran++;
    }

    if (filter && ran == 0) {
        fprint(stderr, "api_test: unknown suite '", filter, "'");
        return 2;
    }
    if (g_failures != 0) {
        fprint(stderr, "api_test: ", g_failures, " FAILURES");
        return 1;
    }
    puts("api_test: PASS");
    return 0;
}
