# graphvex — Repo-Local Living Preferences

## Constitution Link

gist.md (`../../metadata/gists/preferences/gist.md`) is the compressed law list; all universal laws bind this repository. Read one on demand: `./tools/prefs law "<title>"`. Universal laws are inherited, never restated.

Tier: core (R3 GPU Driver). Owns: shaders, GPU dispatch, synchronization, graphics language, element tree, widget compositor. Borrows: Vexspoke CPU/behavior, Relational Engine memory/storage.

## Exclusive Laws

| Law Title | Scope | Enforcement |
| **Unified Graphics Abstraction Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **Strict 0xRRGGBBAA Color Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **SPIR-V Shader Deployment Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **Ecosystem Vulkan Safety Nets Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **Seam Canvas Pixel Mapping Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **R3 Graphics Language & Board Compositor Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **UI Graphics Source Placement Law** | R3 graphics primitives | Mandatory for `graphvex` |
| **Absolute Rendering & Event Bound Law** | R3 element geometry/composition | Mandatory for `graphvex` |
| **Ordered Filter & Scatter Composition Law** | R3 widget compositor | Mandatory for `graphvex` |
| **Independent Scene Cadence Law** | R3 composition/image handoff | Mandatory for `graphvex` |

### Unified Graphics Abstraction Law

* Backend-agnostic graphics primitives (`Image`, `Fence`, `Semaphore`, `Swapchain`, `CommandBuffer`, `CommandQueue`, `Surface`, `GraphicsLayer`) decouple app/UI rendering from Vulkan, Metal, Direct3D.
* `Image`/`Swapchain` keep tightly-packed CPU shadow blocks — zero steady-state allocation (RGBA8 at width * height * 4 Bytes) from the `vexspoke` arena (`Memory_alloc`).
* GPU-GPU ordering (`Semaphore`) advances monotonically, strictly probed, never blocking CPU threads.
* Surfaces/swapchains bridge into OS compositing layers (`CAMetalLayer` via `GraphicsLayer`, `VkIOSurface`, window hooks) by native hardware pixel scaling.
* Never expose driver handles (`VkImage`, `VkDevice`, `MTLDevice`) in client headers.
* Every fence join and timeline wait obeys `FENCE_WAIT_TIMEOUT_NS` (100ms max); indefinite GPU waits (`UINT64_MAX`) prohibited.
* Every image drawn into a destination rect resolves through `Image_fitRect` + `Graphics_drawImageFit` — never a bespoke widget transform.

### Strict 0xRRGGBBAA Color Law

* All packed 32-bit integer colors are strictly `0xRRGGBBAA` (Red bits 31–24, Green 23–16, Blue 15–8, Alpha 7–0).
* Channel order in continuous buffers, pixel shadow arrays (`Image`) and color framebuffers (`ColorBuffer`) is strictly monotonic: 0 Red, 1 Green, 2 Blue, 3 Alpha.
* Legacy `0xAARRGGBB`/`0xBBGGRRAA` prohibited; opaque black `0x000000FFu`; opaque white `0xFFFFFFFFu`; fully transparent/clear `0x00000000u`.
* Unpacking uses monotonic shifts: `Red = (color >> 24) & 0xFFu` … `Alpha = color & 0xFFu`.
* Hardware scanout needs (native BGRA display swapchains) live only inside driver-level presentation adapters; application logic, brushes, meshes, buffers and textures stay strictly RGBA.

### SPIR-V Shader Deployment Law

* GLSL lives in Graphvex's `src/shaders`; umbrella build `../../../tools/b` compiles ahead-of-time SPIR-V into external build-state `shader/`.
* New modules separate responsibilities (`ui`, `shadow`, `compositor`, `filters`, `light`) — never one unbounded shader spanning widget composition and scene lighting.
* Shared color/alpha/coordinate helpers have one implementation.
* Darling and vexspoke own no shader blobs.

### Ecosystem Vulkan Safety Nets Law

* Every Vulkan-touching file carries deterministic handling — same input sequence ⇒ same seam name, decision, log line.
* `VkGuard_check(seam, device, queue, deviceLost)` lives once in `graphvex/src/vulkan/vk_guard.h` (header-only, no struct).
* Under `NDEBUG` the guard is a macro no-op — zero calls, zero branches, unevaluated arguments.
* One fixed decision table for every wait/acquire/submit/present: success advances; timeout-with-signal recovers; timeout-unsignaled drops (dirty state intact), retry next tick; `VK_ERROR_DEVICE_LOST` latches once at the true site.
* Report site is never the cause: on `VK_ERROR_DEVICE_LOST`, read MoltenVK's Metal error before touching code.
* Handles nulled in the same teardown pass.

### Seam Canvas Pixel Mapping Law

* `CAMetalLayer` `drawableSize` and every Vulkan render into the seam canvas, never logical points.
* Seam `CALayer` frame in logical points; `contentsScale` = `backingScaleFactor` (`2.0` on Retina) so native pixels map 1:1.
* The seam canvas is the window's only `CAMetalLayer`; no per-pane surfaces or swapchains.

### R3 Graphics Language & Board Compositor Law

* `graphvex` owns the backend-agnostic graphics language, graphical element tree and widget compositor: placement, isolated groups, images, filters, masks, render dependencies, ordered composition.
* Darling (R4) is the widget interface; it submits Graphvex graphics records, never a competing filter/render compositor.
* `lang/` owns the graphics vocabulary: `device`, `image`, `filter`, `filter_stack`, `compositor`, `graphics_component`, `element_node`, `transform`.
* Naming marks the layer: placement type `GraphicsComponent` (graphvex), never `Component` (darling's interface); container `ElementNode` (graphvex), never darling's container.
* `AUTO` = sentinel with per-class equivalence; `lang/size.h` owns `SIZE_AUTO`; `w == SIZE_AUTO` resolves to the element's AUTO equivalence — its class default.
* `Property` = placement bound; `ui/property.h` owns the rectangle every element carries (x/y/w/h + radius/background/border/shadow/blur).

### UI Graphics Source Placement Law

* Graphical panel/label implementations live under `src/ui/<kind>/`; public vocabulary under `src/lang`.
* Graphvex owns element-tree composition and filter execution under `src/compositor`.
* Editing, focus, layout policy, application behavior remain Darling's; Graphvex borrows R2 public contracts only, never Darling headers.

### Absolute Rendering & Event Bound Law

* Each element has two public geometric bounds — event bound and absolute bound; neither is an offscreen allocation.
* Event bound: resolved layout/hit rectangle in the declared coordinate space; filters never alter it.
* Absolute bound: conservative world-space AABB enclosing this group's and its descendants' paint after their effects, child-content clips and ordered element filters.
* Unknown support requires conservative full-group fallback or explicit rejection, never an invented finite margin.
* Targets retain their world origin; expanded allocations translate coordinates only, never stretching results back into event rectangles.
* Movement, removal, filters and descendant changes invalidate old and new absolute output support; backdrop reads add rendering dependencies.

### Ordered Filter & Scatter Composition Law

* The widget compositor executes ordered filter stacks over isolated graphics groups.
* Backdrop filters consume the composed painter-order scene prefix, excluding self/later siblings; foreground filters consume content/children, excluding own box decoration; element filters consume the assembled element.
* Filter token = `ID16 | payload48`; `filter/filter_type.h` owns canonical operation IDs (`FILTERNAME_ID`).
* Stacks execute left-to-right; no silent reordering.
* Composition and blur use linear-light premultiplied RGBA; weighted contributions add into isolated f32 accumulation, then resolve into one ordinary source-over group composition.
* Filtered backdrop replaces its masked prefix region; never composite the original backdrop twice.

### Independent Scene Cadence Law

* Widget composition and scene-image production run on independent logical schedules; separate OS threads are optional, not implied.
* The compositor consumes the newest completed scene image without forcing a scene tick or waiting for the next image.
* Images and parameter records remain alive until consumers finish, resize/close included.
* Widget changes and completed scene versions invalidate composition independently; scene work must not monopolize widget scheduling.

## Repo-Local Extensions

* None — no managed exceptions to universal laws.

## Readiness Cross-Reference

* Feature readiness: graphvex readiness gist.
* Open blockers: ecosystem blockers gist.
* Proof lawbook: `tests/test-preferences.md` — `./tools/prefs test "<title>"`.
* Evidence ledger: `metadata/gists/test-checklist.md` — `python3 tools/test_checklist.py check`.
