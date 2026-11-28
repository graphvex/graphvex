# graphvex (api), the GPU API

graphvex is **just the API**: backend-agnostic GPU objects (`Graphics`, `Image`, `GraphicsImage`, `GraphicsBackingImage`, `Surface`, `ImagePool`, `ImageArena`, `GraphicsCommand`, `ShaderSource`, `ShaderBytes`, `SecureMemoryPool`, `SecureMemoryArena`) and the drivers that implement them. It draws nothing by itself.

```text
Image (currency) to GraphicsImage (ImagePool) | GraphicsBackingImage (OS backing: video, Surface to CompositeSurface to Frame)
graphvex (api) to collage (rect-first, forward compositor) | meld (retained renderers) | datamosh (video) to darling (UI)
```

## Scope and limitations

- **No swapchain, ever (double-buffered Surface ceiling).** `graphvex/api` is a non-swapchain, image-first graphics API by design. `Surface` inherits `GraphicsBackingImage` and cycles at most **2** OS backing buffers (double-buffered). Triple-buffering is excluded from `Surface` and reserved strictly for `meld` 3D scenes.
- **No shader source files here.** `graphvex/api/shader/` owns shader processing and file/bundle loading (`shader.h`, `shader.c`) only. Compositor shaders belong to `graphvex/compositor/shader/`, and 3D/scene shaders belong to `graphvex/meld/shader/`. Only driver-internal builtin kernels (`metal/builtin.metal`) live inside their driver directory.
- **Isolated domain memory pools.** GPU image slabs (`ImagePool` / `ImageArena` on both Metal and `software-uma`) are standalone OS mappings bracketed by leading and trailing `PROT_NONE` guard pages, completely detached from `re`'s shared CPU `MemoryPool`. CPU-side graphics metadata (`Graphics`, driver contexts, shader blobs, command lists) allocates out of `sandcastles`'s `SecureMemoryPool` / `SecureMemoryArena` (`sandcastles/nio/nio.h`). Each sibling owns its own domain pool: `collage` (`CompositorPool`/`CompositorArena`), `meld` (`ScenePool`/`SceneArena`), `datamosh` (`MediaPool`/`MediaArena`), and `sandcastles` (per-tab untrusted apps).
- **No drawing vocabulary here.** Fonts, shapes, brushes, filters, and UI widgets belong to `compositor` (`collage`) and `darling`; meshes, materials, and scenes belong to `meld`.

## Deliberate seams

1. **Four backends, split by graphics API, not by platform:** `vulkan/`, `metal/`, `direct/`, `wgpu/`. A driver whose API does not exist on a target fails with `#error` there.
2. **Metal drivers are `.m`** (the stated Objective-C exception); Direct3D 12 is Windows-only.
3. **Vendor headers live only in the driver folders.** Public headers expose opaque structs and `Class_verb` functions; no `Vk*`, `MTL*`, `ID3D12*`, or `WGPU*` token appears in any public header.
4. **Shaders are GLSL | MSL | WGSL, SPIR-V is the common target.** Each backend keeps its native interpretation. MSL-only modules are Metal-only by design.
5. **Capabilities are probed once.** Missing capabilities select a stated fallback; ray tracing never enters core.
6. **Every GPU wait is bounded** at 100 ms.

## Layout

`core/` backend-free classes (`Image`, `GraphicsImage`, `GraphicsBackingImage`, `Surface`, `Graphics`) | `vio/` ImagePool/ImageArena/ImageMemory | `nio/` SecureMemoryPool/SecureMemoryArena | `metal/ vulkan/ direct/ wgpu/` drivers | `shader/` shader processing | `tests/`

Laws: [graphvex-preferences.md](graphvex-preferences.md).
