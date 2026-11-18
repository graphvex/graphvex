// api_core_test.c — tests for graphvex/api core GPU objects, 0xRRGGBBAA color,
// 100ms bounded fence wait, bindless slot 0, and GLSL|MSL|WGSL shader routing.

#include "graphvex.h"

static i32 failures;

static fn check(i32 ok, const char *what) {
    if (ok) return;
    fprintf(stderr, "api_core_test: FAIL %s\n", what);
    failures++;
}

i32 main() {
    // 1. All 4 backends exist and report expected capabilities
    GraphicsDevice vk  = GraphicsDevice(BACKEND_VULKAN);
    GraphicsDevice mtl = GraphicsDevice(BACKEND_METAL);
    GraphicsDevice d3d = GraphicsDevice(BACKEND_DIRECT);
    GraphicsDevice wgpu = GraphicsDevice(BACKEND_WGPU);

    check(GraphicsDevice_supports(&vk, GRAPHVEX_CAP_BINDLESS), "Vulkan supports bindless");
    check(!GraphicsDevice_supports(&vk, GRAPHVEX_CAP_RAY_PIPELINE), "Vulkan default excludes ray pipeline (MoltenVK safe)");
    check(GraphicsDevice_supports(&mtl, GRAPHVEX_CAP_RAY_QUERY), "Metal supports native ray query");
    check(GraphicsDevice_supports(&d3d, GRAPHVEX_CAP_RAY_PIPELINE), "D3D12 supports ray pipeline");
    check(!GraphicsDevice_supports(&wgpu, GRAPHVEX_CAP_TIMELINE), "wgpu default excludes timeline semaphores");

    // 2. Strict 0xRRGGBBAA Color Law
    u32 color = ColorRGBA_pack(0x12u, 0x34u, 0x56u, 0x78u);
    check(color == 0x12345678u, "ColorRGBA_pack packs 0xRRGGBBAA");
    check(ColorRGBA_red(color) == 0x12u && ColorRGBA_green(color) == 0x34u &&
          ColorRGBA_blue(color) == 0x56u && ColorRGBA_alpha(color) == 0x78u,
          "ColorRGBA unpacks monotonic R,G,B,A");
    check(COLOR_BLACK_RGBA == 0x000000FFu, "Opaque black is 0x000000FF");

    // 3. BindlessHeap reserves slot 0 for dummy white texture
    BindlessHeap heap = BindlessHeap(16u);
    Image img = Image(256u, 128u);
    check(Image_bindlessSlot(&img) == BINDLESS_DUMMY_WHITE_SLOT, "Unregistered image points to slot 0");
    u32 slot = BindlessHeap_registerImage(&heap, &img);
    check(slot == 1u && Image_bindlessSlot(&img) == 1u, "First registered image gets slot 1");

    // 4. Image_fitRect aspect-preserving fit
    f32 fx = 0, fy = 0, fw = 0, fh = 0;
    Image_fitRect(&img, 0.0f, 0.0f, 100.0f, 100.0f, &fx, &fy, &fw, &fh);
    check(fw == 100.0f && fh == 50.0f && fy == 25.0f, "256x128 image fits centered in 100x100");

    // 5. Fence 100ms bound (Bounded Wait Law)
    Fence fence = Fence();
    check(Fence_waitBounded(&fence, FENCE_WAIT_TIMEOUT_NS), "Fence wait at 100ms succeeds");
    check(!Fence_waitBounded(&fence, FENCE_WAIT_TIMEOUT_NS + 1u), "Fence wait above 100ms is rejected");

    // 6. ShaderSource: MSL-only is Metal-only; GLSL+WGSL works everywhere
    ShaderSource mslOnly = { 0 };
    ShaderSource_setModuleName(&mslOnly, "metal_custom");
    ShaderSource_setAvailableLanguageMask(&mslOnly, 1u << SHADER_LANG_MSL);
    check(ShaderSource_supportsBackend(&mslOnly, BACKEND_METAL), "MSL-only supports Metal");
    check(!ShaderSource_supportsBackend(&mslOnly, BACKEND_VULKAN), "MSL-only rejected on Vulkan");

    ShaderSource dual = { 0 };
    ShaderSource_setModuleName(&dual, "scene/gbuffer");
    ShaderSource_setAvailableLanguageMask(&dual, (1u << SHADER_LANG_GLSL) | (1u << SHADER_LANG_WGSL));
    check(ShaderSource_supportsBackend(&dual, BACKEND_VULKAN) &&
          ShaderSource_supportsBackend(&dual, BACKEND_METAL) &&
          ShaderSource_supportsBackend(&dual, BACKEND_DIRECT) &&
          ShaderSource_supportsBackend(&dual, BACKEND_WGPU),
          "GLSL+WGSL module supports all 4 backends");
    check(ShaderSource_preferredLanguage(&dual, BACKEND_WGPU) == SHADER_LANG_WGSL,
          "wgpu prefers native WGSL when available");

    return failures ? 1 : 0;
}
