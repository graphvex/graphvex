# graphvex (api) — the GPU API

> Branch `another-rewrite`. A clean rewrite; `../api_old/` is the behaviour reference. Nothing here is implemented yet — see `_notes/graphvex/MANIFEST.md` for the work order.

graphvex is **just the API**: backend-agnostic GPU objects (`Image`, `GpuBuffer`, `Pipeline`, `Pass`, `CommandBuffer`, `Fence`, `Semaphore`, `Swapchain`, …) and the drivers that implement them. It draws nothing by itself.

```text
graphvex (api) → collage (rect-first, forward compositor) | meld (retained renderers) → darling (UI, incl. 3D panel)
```

## Deliberate seams

1. **Four backends, split by graphics API, not by platform:** `vulkan/`, `metal/`, `direct/`, `wgpu/` (wgpu-native). A driver whose API does not exist on a target fails with `#error` there. This is intentional and does not violate One File, All Platforms: platform branches inside a driver stay in that driver's file.
2. **Metal drivers are `.m`** (the stated Objective-C exception); Direct3D 12 is Windows-only.
3. **Vendor headers live only in the driver folders.** Public headers expose opaque structs and `Class_verb` functions; no `Vk*`, `MTL*`, `ID3D12*` or `WGPU*` token in any public header.
4. **Shaders are GLSL | MSL | WGSL, SPIR-V is the common target.** Each backend keeps its native interpretation. MSL-only modules are Metal-only by design.
5. **Capabilities are probed once** (bindless, timeline, mesh shader, ray query, …). Missing capabilities select a stated fallback; ray tracing never enters core.
6. **Every GPU wait is bounded** at 100 ms.
7. **No drawing vocabulary here.** Fonts, shapes, brushes, filters and elements are collage's; meshes and scenes are meld's.

## Layout

`core/` backend-free classes · `vulkan/ metal/ direct/ wgpu/` drivers · `shader/` module registry · `tests/`

Laws: [graphvex-preferences.md](graphvex-preferences.md).
