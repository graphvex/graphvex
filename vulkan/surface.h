#ifndef GRAPHICS_SURFACE_H
#define GRAPHICS_SURFACE_H

#include "vex.h"
#include "graphvex/lang/image.h"
#include "graphvex/lang/board.h"

// graphvex R3 — vulkan/surface.h
//
// *** NO SWAPCHAIN. *** A Surface is a host-borrowed native destination
// (a CAMetalLayer on Apple, HWND on Windows, xcb window on Linux) plus ONE
// retained present Image that we render into. Surface_present() hands the
// completed image to the host seam (the Frame blits a Metal drawable). We never
// create, acquire, or present a VkSwapchainKHR — there is no such call in this
// codebase (the Single-Seam Canvas Law).

typedef struct Surface Surface;

// The host present seam. graphvex owns no platform code and never includes
// Metal/CoreAnimation, so the layer that owns the borrowed native destination
// (an R1 window / R4 frame) installs a blit that copies the retained present
// Image into it — a CAMetalLayer contents update on Apple. The callback reads
// Surface_handle and Surface_presentImage, writes the destination, and returns
// true only when a frame was actually handed over.
typedef bool (*SurfacePresentFn)(Surface *surface, any userdata);
typedef u64 (*SurfaceClockFn)(any userdata); // monotonic nanoseconds

Surface *Surface_0();
Surface *Surface_3(any native, u32 width, u32 height);
#define Surface(...) CONSTRUCTOR_DISPATCH(Surface, __VA_ARGS__)
fn Surface_destroy(Surface *surface);

bool Surface_resize(Surface *surface, u32 width, u32 height);
u32 Surface_width(const Surface *surface);    // native px
u32 Surface_height(const Surface *surface);
bool Surface_isValid(const Surface *surface);
any Surface_handle(const Surface *surface);      // borrowed native destination
Image *Surface_presentImage(Surface *surface);     // retained target to render into

// Register the host blit. The callback + userdata are borrowed, never owned;
// passing NULL clears the seam. Idempotent (re-register replaces).
fn Surface_onPresent(Surface *surface, SurfacePresentFn fn, any userdata);

// Hand the completed present image to the host seam: invoke the registered
// blit. Returns false when no seam is installed (an offscreen surface has
// nowhere to present) or when the blit reports failure. Never a swapchain
// present — the host owns the drawable.
bool Surface_present(Surface *surface);
// Owner-thread policy. -1 = no imposed ceiling; positive = FPS ceiling.
// No sleeps, no scene-thread pacing. Deferred invalidations coalesce.
fn Surface_setFPSCap(Surface *surface, int fps);
int Surface_getFPSCap(const Surface *surface);
u64 Surface_getPresentCount(const Surface *surface);
fn Surface_poll(Surface *surface);
// Fresh offscreen/capture rendering; host publication still respects the cap.
fn Surface_revalidateNow(Surface *surface);
// Optional deterministic clock; NULL restores CLOCK_MONOTONIC and resets deadline.
fn Surface_setClock(Surface *surface, SurfaceClockFn fn, any userdata);

// ── revalidation (the render cascade) ───────────────────────────────────────
// A surface may own several boards (scene + content). Surface_revalidate is the
// one call a Frame makes: revalidate every attached board (which renders its
// scene/content into the board), then hand the completed present image to the
// host. Boards are BORROWED — the surface never frees one.
fn Surface_addBoard(Surface *surface, Board *board);
fn Surface_removeBoard(Surface *surface, Board *board);
fn Surface_revalidate(Surface *surface);   // Boards_revalidate, then present

#endif // GRAPHICS_SURFACE_H
