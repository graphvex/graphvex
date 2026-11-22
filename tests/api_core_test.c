/**
 * api_core_test.c -- tests for the unified, swapchain-free Graphics API over vio/:
 * zero-copy Graphics_crop, Graphics_copy overloads, scissor-clipped clear/copy,
 * mandatory bindless slots, Graphics_command/dispatch/draw/present, 0xRRGGBBAA
 * color law, and ShaderSource language routing.
 */

#include "graphvex.h"

static i32 failures;

static fn check(i32 ok, const utf8 *what) {
    if (ok)
        return;
    fprintf(stderr, "api_core_test: FAIL %s\n", what);
    failures++;
}

i32 main() {
    u32 color = ColorRGBA_pack(0x12u, 0x34u, 0x56u, 0x78u);
    check(color == 0x12345678u, "ColorRGBA_pack packs 0xRRGGBBAA");
    check(ColorRGBA_red(color) == 0x12u && ColorRGBA_green(color) == 0x34u &&
          ColorRGBA_blue(color) == 0x56u && ColorRGBA_alpha(color) == 0x78u,
          "ColorRGBA unpacks monotonic R,G,B,A");
    check(COLOR_BLACK_RGBA == 0x000000FFu, "Opaque black is 0x000000FF");

    VideoPool *pool = VideoPool_createSoftware(4u * 1024u * 1024u);
    check(pool != nullptr, "VideoPool_createSoftware succeeds");
    VideoArena *arena = VideoArena_create(pool, VIDEO_MEMORY_SHARED, 0u);
    check(arena != nullptr, "VideoArena_create succeeds");
    Graphics *g = Graphics_create(pool);
    check(g != nullptr, "Graphics_create succeeds");

    GraphicsImage canvas = Graphics_createImage(g, arena, 16u, 16u, PIXEL_FORMAT_RGBA8_UNORM);
    check(GraphicsImage_isValid(&canvas), "Graphics_createImage produces valid vpointer image");
    check(GraphicsImage_bindlessSlot(&canvas) == 1u, "First image auto-registered at bindless slot 1");
    check(!GraphicsImage_isSubview(&canvas), "Fresh image is not a subview");

    GraphicsImage wide = Graphics_createImage(g, arena, 256u, 128u, PIXEL_FORMAT_RGBA8_UNORM);
    check(GraphicsImage_bindlessSlot(&wide) == 2u, "Second image gets bindless slot 2");
    f32 fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
    GraphicsImage_fitRect(&wide, 0.0f, 0.0f, 100.0f, 100.0f, &fx, &fy, &fw, &fh);
    check(fw == 100.0f && fh == 50.0f && fy == 25.0f, "256x128 image fits centered in 100x100");

    Graphics_setTarget(g, &canvas);
    check(Graphics_clear(g, 0x102030FFu), "Full clear queued");
    Graphics_setScissor(g, 4, 4, 12, 12);
    check(Graphics_clear(g, 0xFF00AAFFu), "Scissor clear queued");
    Graphics_resetScissor(g);
    check(Graphics_submit(g), "Graphics_submit executes clears");
    check(Graphics_frameIndex(g) == 1u, "Frame index advances on submit");

    u8 *px = (u8*) VideoMemory_map(pool, GraphicsImage_data(&canvas));
    check(px != nullptr, "UMA map returns direct CPU pointer to canvas");
    check(px[0] == 0x10u && px[1] == 0x20u && px[2] == 0x30u && px[3] == 0xFFu,
          "Outer pixel keeps base clear color");
    bytes centerOff = (bytes) 4u * (bytes) GraphicsImage_strideBytes(&canvas) + 4u * 4u;
    check(px[centerOff] == 0xFFu && px[centerOff + 1u] == 0x00u &&
          px[centerOff + 2u] == 0xAAu && px[centerOff + 3u] == 0xFFu,
          "Scissored inner pixel has second clear color");

    GraphicsImage view = Graphics_crop(&canvas, 4, 4, 12, 12);
    check(GraphicsImage_isValid(&view) && GraphicsImage_isSubview(&view),
          "Graphics_crop produces valid zero-copy subview");
    check(GraphicsImage_width(&view) == 8u && GraphicsImage_height(&view) == 8u,
          "Cropped view has 8x8 extent");
    check(GraphicsImage_strideBytes(&view) == GraphicsImage_strideBytes(&canvas),
          "Cropped view preserves parent strideBytes");
    check(GraphicsImage_data(&view) == GraphicsImage_data(&canvas) + centerOff,
          "Cropped view vpointer points directly to (4,4) with zero copy");

    GraphicsImage fullCopy = Graphics_copy(g, arena, &canvas);
    GraphicsImage cropCopy = Graphics_copy(g, arena, &canvas, 4, 4, 12, 12);
    check(Graphics_submit(g), "Graphics_submit executes copies");
    check(GraphicsImage_isValid(&fullCopy) && !GraphicsImage_isSubview(&fullCopy),
          "Graphics_copy(g, arena, src) allocates standalone tight image");
    check(GraphicsImage_width(&cropCopy) == 8u && GraphicsImage_height(&cropCopy) == 8u &&
          GraphicsImage_strideBytes(&cropCopy) == 8u * 4u,
          "Graphics_copy(g, arena, src, x0, y0, x1, y1) packs tightly");
    u8 *cropPx = (u8*) VideoMemory_map(pool, GraphicsImage_data(&cropCopy));
    check(cropPx != nullptr && cropPx[0] == 0xFFu && cropPx[1] == 0x00u &&
          cropPx[2] == 0xAAu && cropPx[3] == 0xFFu,
          "Cropped copy pixel (0,0) matches canvas (4,4)");

    GraphicsImage dst = Graphics_createImage(g, arena, 16u, 16u, PIXEL_FORMAT_RGBA8_UNORM);
    Graphics_setTarget(g, &dst);
    check(Graphics_clear(g, COLOR_BLACK_RGBA), "Clear dst to black");
    Graphics_setScissor(g, 0, 0, 4, 4);
    check(Graphics_copyTo(g, &dst, 0, 0, &cropCopy), "Scissored copyTo queued");
    Graphics_resetScissor(g);
    check(Graphics_submit(g), "Submit scissored copyTo");
    u8 *dstPx = (u8*) VideoMemory_map(pool, GraphicsImage_data(&dst));
    check(dstPx[0] == 0xFFu && dstPx[2] == 0xAAu, "Copied pixel inside scissor");
    check(dstPx[5u * 4u] == 0x00u && dstPx[5u * 4u + 3u] == 0xFFu,
          "Pixel outside scissor untouched");

    u32 blurShader = Graphics_registerShader(g, "collage/blur_smol", SHADER_STAGE_COMPUTE);
    u32 meshShader = Graphics_registerShader(g, "meld/mesh", SHADER_STAGE_VERTEX);
    check(blurShader == 1u && meshShader == 2u, "Shaders register without vendor handles");
    vpointer dummyVerts = VideoArena_alloc(arena, 256u, 32u);
    check(Graphics_dispatch(g, blurShader, 8u, 8u, 1u, 0u, 0u), "Graphics_dispatch queues compute");
    check(Graphics_draw(g, meshShader, dummyVerts, 0u, 3u, 0u, 0u), "Graphics_draw queues draw");
    check(Graphics_present(g, nullptr, &canvas), "Graphics_present flushes and presents with no swapchain");

    ShaderSource mslOnly = { 0 };
    ShaderSource_setModuleName(&mslOnly, "metal_custom");
    ShaderSource_setAvailableLanguageMask(&mslOnly, 1u << SHADER_LANG_MSL);
    check(ShaderSource_supportsLanguage(&mslOnly, SHADER_LANG_MSL), "MSL-only supports MSL");
    check(!ShaderSource_supportsLanguage(&mslOnly, SHADER_LANG_GLSL), "MSL-only rejected on GLSL/SPIR-V");

    ShaderSource dual = { 0 };
    ShaderSource_setModuleName(&dual, "collage/blur");
    ShaderSource_setAvailableLanguageMask(&dual, (1u << SHADER_LANG_GLSL) | (1u << SHADER_LANG_WGSL));
    check(ShaderSource_supportsLanguage(&dual, SHADER_LANG_GLSL) &&
          ShaderSource_supportsLanguage(&dual, SHADER_LANG_MSL) &&
          ShaderSource_supportsLanguage(&dual, SHADER_LANG_WGSL),
          "GLSL+WGSL module supports all languages");
    check(ShaderSource_preferredLanguage(&dual, SHADER_LANG_WGSL) == SHADER_LANG_WGSL,
          "Prefers native WGSL when available");

    ShaderBytes mslFile = Shader_loadFile("vex/graphvex/api/shader/msl/builtin.metal");
    check(ShaderBytes_isValid(&mslFile) && ShaderBytes_length(&mslFile) > 100u,
          "Shader_loadFile reads shader/msl/builtin.metal via re/io/file");
    ShaderBytes_destroy(&mslFile);
    check(!ShaderBytes_isValid(&mslFile), "ShaderBytes_destroy resets handle");

    Graphics_destroy(g);
    VideoArena_destroy(arena);
    VideoPool_destroy(pool);

    return failures ? 1 : 0;
}
