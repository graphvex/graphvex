# graphvex (api), the GPU API

graphvex is **just the API**: backend-agnostic GPU objects (`Graphics`, `GraphicsImage`, `VideoPool`, `VideoArena`, `GraphicsCommand`, `ShaderSource`, `ShaderBytes`, `SecureMemoryPool`, `SecureMemoryArena`) and the drivers that implement them. It draws nothing by itself.

```text
graphvex (api) to collage (rect-first, forward compositor) | meld (retained renderers) to darling (UI, incl. 3D panel)
```

## Scope and limitations

- **No swapchain, ever.** `graphvex/api` is a non-swapchain, image-first graphics API by design. Callers render directly into `GraphicsImage` storage and publish to a native surface via `Graphics_present`. Double-buffering images (two active backing arenas and at most two platform drawables) is the ceiling -- triple-buffered swapchains are intentionally excluded.
- **No shader source files here.** `graphvex/api/shader/` owns shader processing and file/bundle loading (`shader.h`, `shader.c`) only. Compositor shaders belong to `graphvex/compositor/shader/`, and 3D/scene shaders belong to `graphvex/meld/shader/`. Only driver-internal builtin kernels (`metal/builtin.metal`) live inside their driver directory.
- **Isolated graphics memory (5-pool model).** GPU texture, framebuffer, atlas, and buffer slabs (`VideoPool` / `VideoArena` on both Metal and `software-uma`) are standalone OS mappings bracketed by leading and trailing `PROT_NONE` guard pages, completely detached from `re`'s shared CPU `MemoryPool`. CPU-side graphics metadata (`Graphics`, driver contexts, shader blobs, command lists) allocates out of `sandcastles`'s `SecureMemoryPool` / `SecureMemoryArena` (`sandcastles/nio/nio.h`). In the broader stack, `meld` (3D scenes, including `<canvas>` / Three.js for `darling-for-web`), `datamosh` (video/media), and `sandcastles` (per-tab untrusted apps) each own their own standalone pools.
- **No drawing vocabulary here.** Fonts, shapes, brushes, filters, and UI widgets belong to `compositor` (`collage`) and `darling`; meshes, materials, and scenes belong to `meld`.

## Deliberate seams

1. **Four backends, split by graphics API, not by platform:** `vulkan/`, `metal/`, `direct/`, `wgpu/`. A driver whose API does not exist on a target fails with `#error` there.
2. **Metal drivers are `.m`** (the stated Objective-C exception); Direct3D 12 is Windows-only.
3. **Vendor headers live only in the driver folders.** Public headers expose opaque structs and `Class_verb` functions; no `Vk*`, `MTL*`, `ID3D12*`, or `WGPU*` token appears in any public header.
4. **Shaders are GLSL | MSL | WGSL, SPIR-V is the common target.** Each backend keeps its native interpretation. MSL-only modules are Metal-only by design.
5. **Capabilities are probed once.** Missing capabilities select a stated fallback; ray tracing never enters core.
6. **Every GPU wait is bounded** at 100 ms.

## Layout

`core/` backend-free classes | `vio/` VideoPool/VideoArena/VideoMemory | `nio/` SecureMemoryPool/SecureMemoryArena | `metal/ vulkan/ direct/ wgpu/` drivers | `shader/` shader processing | `tests/`

Laws: [graphvex-preferences.md](graphvex-preferences.md).
