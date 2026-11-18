# graphvex — Repo-Local Living Preferences

## Constitution Link

gist.md (`../../../../metadata/gists/preferences/gist.md`) is the compressed law list; all universal laws bind this repository. Read one on demand: `./tools/prefs law "<title>"`. Universal laws are inherited, never restated.

## Tier and Ownership

**Tier:** `core` (GPU Driver). graphvex is **just the API**.
**Owns:** the backend-agnostic GPU object classes (`core/`), the four drivers (`vulkan/`, `metal/`, `direct/`, `wgpu/`), the shader module registry (`shader/`).
**Does not own:** drawing vocabulary, UI composition or scenes. Those live in sibling core repos:
* **collage** (`../compositor/`) — rect-first, forward-rendered compositor; owns `lang/`, `paint/`, `text/`, elements, filters.
* **meld** (`../meld/`) — retained 2D/3D renderer; owns `Mesh`, `Meshlet`, scenes, spaces, terrain.
Borrows: lib (`vex.h`), re (`Memory_*`), vexspoke (CPU/behavior). Includes point down only; collage and meld include graphvex, never the reverse, and never each other.

Supersedes `../api_old/graphvex-preferences.md` (frozen). Rewrite branch: `another-rewrite`.

## Directory Headers

* One `.h` per directory, named after the directory; the repo root header is `graphvex.h`.
* Consumers include the root header or a directory header, never an individual class header.

## Exclusive Laws

| Law Title | Scope | Enforcement |
| **Unified Graphics Abstraction Law** | core GPU Driver | Mandatory for `graphvex` |
| **Driver Axis Seam Law** | `vulkan/`, `metal/`, `direct/`, `wgpu/` | Mandatory for `graphvex` |
| **Strict 0xRRGGBBAA Color Law** | core GPU Driver | Mandatory for `graphvex` |
| **SPIR-V Shader Deployment Law** | `shader/`, shader sources | Mandatory for `graphvex`, collage, meld |
| **Ecosystem Vulkan Safety Nets Law** | every driver | Mandatory for `graphvex` |
| **Seam Canvas Pixel Mapping Law** | core GPU Driver | Mandatory for `graphvex` |

### Unified Graphics Abstraction Law

* Backend-agnostic primitives (`Image`, `Fence`, `Semaphore`, `Swapchain`, `CommandBuffer`, `CommandQueue`, `Surface`, `GraphicsLayer`) decouple app/UI rendering from Vulkan, Metal, Direct3D and WebGPU (wgpu).
* `Image`/`Swapchain` keep tightly-packed CPU shadow blocks — zero steady-state allocation (RGBA8 at width * height * 4 Bytes) from the re arena (`Memory_alloc`).
* GPU-GPU ordering (`Semaphore`) advances monotonically, strictly probed, never blocking CPU threads.
* Surfaces/swapchains bridge into OS compositing layers (`CAMetalLayer` via `GraphicsLayer`, window hooks) by native hardware pixel scaling.
* Never expose driver handles (`VkImage`, `VkDevice`, `MTLDevice`, `ID3D12Device`, `WGPUDevice`) in client headers.
* Every fence join and timeline wait obeys `FENCE_WAIT_TIMEOUT_NS` (100ms max); indefinite GPU waits (`UINT64_MAX`) prohibited.
* graphvex holds no drawing vocabulary: no meshes, fonts, shapes, brushes, strokes, elements or filters.
* Optional features are `Capability` bits probed once cold; a false bit selects a stated fallback, never an `#ifdef` in a client.

### Driver Axis Seam Law

* Drivers are split by graphics API (`vulkan/`, `metal/`, `direct/`, `wgpu/`), not by platform.
* A driver whose API does not exist on a target fails with `#error` on that target.
* This is a deliberate seam under the Conflict Triage Law and does not breach the One File, All Platforms Law: platform branches inside a driver still live in that driver's one file.
* Only the four driver folders may include vendor headers.
* Metal's Objective-C bodies stay `.m` behind the same header (the stated Objective-C exception).
* wgpu uses wgpu-native.

### Strict 0xRRGGBBAA Color Law

* All packed 32-bit integer colors are strictly `0xRRGGBBAA` (Red bits 31–24, Green 23–16, Blue 15–8, Alpha 7–0).
* Channel order in continuous buffers, pixel shadow arrays (`Image`) and color framebuffers is strictly monotonic: 0 Red, 1 Green, 2 Blue, 3 Alpha.
* Legacy `0xAARRGGBB`/`0xBBGGRRAA` prohibited; opaque black `0x000000FFu`; opaque white `0xFFFFFFFFu`; fully transparent/clear `0x00000000u`.
* Unpacking uses monotonic shifts: `Red = (color >> 24) & 0xFFu` … `Alpha = color & 0xFFu`.
* Native BGRA display swapchains live only inside driver-level presentation adapters; application logic, buffers and textures stay strictly RGBA.

### SPIR-V Shader Deployment Law

* Shader sources are GLSL, MSL or WGSL; SPIR-V is the common target.
* The umbrella build (`../../../tools/b`) compiles ahead of time into external build-state `shader/`, choosing the native source per backend when present and cross-compiling otherwise (glslang, spirv-cross, naga/tint, dxc).
* MSL-only modules are Metal-only by design; there is no MSL → SPIR-V path.
* A module has one name and one or more sources; the build records which source produced each blob.
* Modules separate responsibilities — meld: `scene`, `shadow`, `terrain`, `portal`, `procedural`, `post`, `sprite`, `pick`; collage: `compose`, `filter`, `source`, `mask`. Never one unbounded shader spanning widget composition and scene lighting.
* Shared color/alpha/coordinate helpers have one implementation per language.
* Darling and vexspoke own no shader blobs.

### Ecosystem Vulkan Safety Nets Law

* Every Vulkan-touching file carries deterministic handling — same input sequence ⇒ same seam name, decision, log line.
* `VkGuard_check(seam, device, queue, deviceLost)` lives once in `vulkan/vk_guard.h` (header-only, no struct).
* Under `NDEBUG` the guard is a macro no-op — zero calls, zero branches, unevaluated arguments.
* One fixed decision table for every wait/acquire/submit/present: success advances; timeout-with-signal recovers; timeout-unsignaled drops (dirty state intact), retry next tick; device loss latches once at the true site.
* Every Metal, Direct3D 12 and wgpu file carries the same decision table through its `mtl_guard.h`, `d3d_guard.h`, `wgpu_guard.h`; the device-lost equivalents (`MTLCommandBufferErrorDeviceRemoved`, `DXGI_ERROR_DEVICE_REMOVED`, the wgpu device-lost callback) latch once.
* Report site is never the cause: on Vulkan `VK_ERROR_DEVICE_LOST`, read MoltenVK's Metal error before touching code.
* Handles nulled in the same teardown pass.

### Seam Canvas Pixel Mapping Law

* `CAMetalLayer` `drawableSize` and every driver render into the seam canvas, never logical points.
* Seam `CALayer` frame in logical points; `contentsScale` = `backingScaleFactor` (`2.0` on Retina) so native pixels map 1:1.
* The seam canvas is the window's only presentable surface; no per-pane surfaces or swapchains.

## Repo-Local Extensions

* None — no managed exceptions to universal laws. (The Driver Axis Seam Law is an exclusive law, not an exception.)

## Moved Laws

The widget-compositor laws moved to collage with their execution: Graphics Language & Board Compositor, UI Graphics Source Placement, Absolute Rendering & Event Bound, Ordered Filter & Scatter Composition. Independent Scene Cadence binds collage (consumer) and meld (producer). See `../compositor/collage-preferences.md` and `../meld/meld-preferences.md`.

## Readiness Cross-Reference

* Feature readiness: graphvex readiness gist.
* Open blockers: ecosystem blockers gist.
* Proof lawbook: `tests/test-preferences.md` — `./tools/prefs test "<title>"`.
* Evidence ledger: `metadata/gists/test-checklist.md` — `python3 tools/test_checklist.py check`.
