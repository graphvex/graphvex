#include "vex.h"
#include "vulkan/surface.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>


;;INTENTION("Surface caps coalesce render/present demand without sleeping or "
            "retiming independent scene workers. The host polls pending demand; "
            "offscreen capture can refresh pixels without bypassing publication caps.")

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Surface (vulkan/surface.c)
 * ============================================================================
 * The presentation seam. *** NO SWAPCHAIN. *** A Surface is a host-borrowed
 * native destination (a CAMetalLayer on Apple, HWND on Windows, an xcb window
 * on Linux) plus ONE retained present Image we render into. Surface_present()
 * hands the completed image to the host seam; we never create, acquire, or
 * present a VkSwapchainKHR (the Single-Seam Canvas Law).
 *
 * A surface may carry several boards (scene + content). Surface_revalidate is
 * the one call a Frame makes: revalidate each attached board — which renders
 * its scene/content into the board — then present the finished image. Boards
 * are BORROWED; the surface never frees one.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Surface (vulkan/surface.c)
 * ============================================================================
 * Host-borrowed native destination + one retained present Image + borrowed
 * boards.
 *
 * STRUCT FIELDS:
 * ----------------------------------------------------------------------------
 *   any native;      // borrowed CAMetalLayer / HWND / xcb window
 *   u32 width, height; // native px
 *   Image   *present;     // the retained target we render into (owned)
 *   bool     presented;   // diagnostic: did the last present succeed
 *   SurfacePresentFn presentFn; // borrowed host blit (NULL = offscreen)
 *   any presentUser; // passed to presentFn
 *   Board  **boards;      // borrowed layers (owned by the caller)
 *   i32      boardCount, boardCap;
 *
 * FUNCTION REGISTRY (exported by vulkan/surface.h):
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Surface_0, Surface_3, Surface_destroy
 * Core:
 *   - Surface_resize, Surface_onPresent, Surface_present
 * Queries / Getters:
 *   - Surface_width, Surface_height, Surface_isValid, Surface_handle,
 *     Surface_presentImage
 * Revalidation:
 *   - Surface_addBoard, Surface_removeBoard, Surface_revalidate
 * ============================================================================
 */

struct Surface {
    any native;      // borrowed CAMetalLayer / HWND / xcb window
    u32 width;
    u32 height;
    Image *present;    // the retained target we render into
    bool presented;    // diagnostic
    SurfacePresentFn presentFn;   // borrowed host blit (nullable = offscreen)
    any presentUser;            // passed to presentFn
    Board **boards;    // borrowed layers composited into the present image
    i32 boardCount, boardCap;
    i32 fpsCap;
    u64 lastPresent, presentCount;
    bool pending;
    SurfaceClockFn clockFn;
    any clockUser;
};

;;GETTER
static inline any Surface_native(const Surface *self) { return (*self).native; }
;;SETTER
static inline fn Surface_setNative(Surface *self, any v) { (*self).native = v; }
;;SETTER
static inline fn Surface_setWidth(Surface *self, u32 v) { (*self).width = v; }
;;SETTER
static inline fn Surface_setHeight(Surface *self, u32 v) { (*self).height = v; }
;;GETTER
static inline Image *Surface_presentField(const Surface *self) { return (*self).present; }
;;SETTER
static inline fn Surface_setPresent(Surface *self, Image *v) { (*self).present = v; }
;;GETTER
static inline bool Surface_presented(const Surface *self) { return (*self).presented; }
;;SETTER
static inline fn Surface_setPresented(Surface *self, bool v) { (*self).presented = v; }
;;GETTER
static inline SurfacePresentFn Surface_presentFn(const Surface *self) { return (*self).presentFn; }
;;SETTER
static inline fn Surface_setPresentFn(Surface *self, SurfacePresentFn v) { (*self).presentFn = v; }
;;GETTER
static inline any Surface_presentUser(const Surface *self) { return (*self).presentUser; }
;;SETTER
static inline fn Surface_setPresentUser(Surface *self, any v) { (*self).presentUser = v; }
;;GETTER
static inline Board **Surface_boards(const Surface *self) { return (*self).boards; }
;;SETTER
static inline fn Surface_setBoards(Surface *self, Board **v) { (*self).boards = v; }
;;GETTER
static inline i32 Surface_boardCount(const Surface *self) { return (*self).boardCount; }
;;SETTER
static inline fn Surface_setBoardCount(Surface *self, i32 v) { (*self).boardCount = v; }
;;GETTER
static inline i32 Surface_boardCap(const Surface *self) { return (*self).boardCap; }
;;SETTER
static inline fn Surface_setBoardCap(Surface *self, i32 v) { (*self).boardCap = v; }
;;GETTER
static inline i32 Surface_fpsCap(const Surface *self) { return (*self).fpsCap; }
;;SETTER
static inline fn Surface_setFpsCap(Surface *self, i32 v) { (*self).fpsCap = v; }
;;GETTER
static inline u64 Surface_lastPresent(const Surface *self) { return (*self).lastPresent; }
;;SETTER
static inline fn Surface_setLastPresent(Surface *self, u64 v) { (*self).lastPresent = v; }
;;GETTER
static inline u64 Surface_presentCount(const Surface *self) { return (*self).presentCount; }
;;SETTER
static inline fn Surface_setPresentCount(Surface *self, u64 v) { (*self).presentCount = v; }
;;GETTER
static inline bool Surface_pending(const Surface *self) { return (*self).pending; }
;;SETTER
static inline fn Surface_setPending(Surface *self, bool v) { (*self).pending = v; }
;;GETTER
static inline SurfaceClockFn Surface_clockFn(const Surface *self) { return (*self).clockFn; }
;;SETTER
static inline fn Surface_setClockFn(Surface *self, SurfaceClockFn v) { (*self).clockFn = v; }
;;GETTER
static inline any Surface_clockUser(const Surface *self) { return (*self).clockUser; }
;;SETTER
static inline fn Surface_setClockUser(Surface *self, any v) { (*self).clockUser = v; }

// Creates an offscreen surface with a 1x1 retained image.
Surface *Surface_0() { return Surface_3(nullptr, 0, 0); }

// Creates a surface around a borrowed native handle and allocates its retained image.
Surface *Surface_3(any native, u32 width, u32 height) {
    Surface *s = calloc(1, sizeof *s);
    if (!s) return nullptr;
    Surface_setNative(s, native);
    Surface_setFpsCap(s, -1);
    Surface_setWidth(s, width);
    Surface_setHeight(s, height);
    ImageDesc d = {width ? width : 1u, height ? height : 1u, IMAGE_FORMAT_RGBA8,
                   IMAGE_USAGE_RENDER | IMAGE_USAGE_TRANSFER | IMAGE_USAGE_SAMPLED};
    Image *present = Image_new(&d);
    if (!present) {
        free(s);
        return nullptr;
    }
    Surface_setPresent(s, present);
    return s;
}

// Destroys the retained image and board directory without destroying borrowed boards or native handle.
fn Surface_destroy(Surface *surface) {
    if (!surface) return;
    Image_destroy(Surface_presentField(surface));
    free(Surface_boards(surface));
    free(surface);
}

// Updates native-pixel dimensions and resizes the retained image, using at least 1x1 backing.
bool Surface_resize(Surface *surface, u32 width, u32 height) {
    if (!surface || !Surface_presentField(surface)) return false;
    Surface_setWidth(surface, width);
    Surface_setHeight(surface, height);
    return Image_resize(Surface_presentField(surface), width ? width : 1u, height ? height : 1u);
}

// Returns the configured native-pixel width, or zero for null.
u32 Surface_width(const Surface *surface) { return surface ? (*surface).width : 0u; }
// Returns the configured native-pixel height, or zero for null.
u32 Surface_height(const Surface *surface) { return surface ? (*surface).height : 0u; }
// Reports whether the retained presentation image exists.
bool Surface_isValid(const Surface *surface) { return surface && Surface_presentField(surface) != nullptr; }
// Returns the borrowed native host handle.
any Surface_handle(const Surface *surface) { return surface ? Surface_native(surface) : nullptr; }
// Returns the retained presentation image as a borrowed pointer.
Image *Surface_presentImage(Surface *surface) { return surface ? Surface_presentField(surface) : nullptr; }

// Installs the host presentation callback and its borrowed userdata.
fn Surface_onPresent(Surface *surface, SurfacePresentFn fn, any userdata) {
    if (!surface) return;
    Surface_setPresentFn(surface, fn);
    Surface_setPresentUser(surface, userdata);
}

// Reads time from the configured clock callback or the monotonic system clock.
static u64 surfaceNow(Surface *surface) {
    if (Surface_clockFn(surface)) return Surface_clockFn(surface)(Surface_clockUser(surface));
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
}

// Reports whether the FPS cap permits another presentation at the current time.
static bool surfaceDue(Surface *surface) {
    i32 cap = Surface_fpsCap(surface);
    return cap == -1 || !Surface_presentCount(surface) ||
        surfaceNow(surface) - Surface_lastPresent(surface) >=
        (1000000000ULL + (u64)cap - 1) / (u64)cap;
}

// Sets the presentation cap; -1 disables it and nonpositive alternatives are ignored.
fn Surface_setFPSCap(Surface *surface, i32 fps) {
    if (!surface || (fps != -1 && fps <= 0) || Surface_fpsCap(surface) == fps) return;
    Surface_setFpsCap(surface, fps);
}
// Returns the configured cap, or zero when surface is null.
i32 Surface_getFPSCap(const Surface *surface) { return surface ? Surface_fpsCap(surface) : 0; }
// Returns the count of successful host presentation callbacks.
u64 Surface_getPresentCount(const Surface *surface) { return surface ? Surface_presentCount(surface) : 0; }
// Installs a clock callback and resets present timing and count for deterministic scheduling.
fn Surface_setClock(Surface *surface, SurfaceClockFn fn, any userdata) {
    if (!surface) return;
    Surface_setClockFn(surface, fn);
    Surface_setClockUser(surface, userdata);
    Surface_setLastPresent(surface, 0);
    Surface_setPresentCount(surface, 0);
}

// Hands the retained image to the host when due; a missing callback or failed callback returns false.
bool Surface_present(Surface *surface) {
    if (!surface || !Surface_presentField(surface)) return false;
    if (!surfaceDue(surface)) { Surface_setPending(surface, true); return false; }
    // The host owns the drawable; we only hand it the finished image. With no
    // blit installed (an offscreen surface) there is nowhere to present.
    bool ok = Surface_presentFn(surface)
        ? Surface_presentFn(surface)(surface, Surface_presentUser(surface))
        : false;
    Surface_setPresented(surface, ok);
    if (ok) {
        Surface_setLastPresent(surface, surfaceNow(surface));
        Surface_setPresentCount(surface, Surface_presentCount(surface) + 1);
        Surface_setPending(surface, false);
    }
    return ok;
}

// ── revalidation ────────────────────────────────────────────────────────────
// Appends a borrowed board to the surface's ordered revalidation list.
fn Surface_addBoard(Surface *surface, Board *board) {
    if (!surface || !board) return;
    if (Surface_boardCount(surface) == Surface_boardCap(surface)) {
        i32 cap = Surface_boardCap(surface) ? Surface_boardCap(surface) * 2 : 2;
        Board **grown = realloc(Surface_boards(surface), (bytes)cap * sizeof *grown);
        if (!grown) return;
        Surface_setBoards(surface, grown);
        Surface_setBoardCap(surface, cap);
    }
    i32 count = Surface_boardCount(surface);
    Surface_boards(surface)[count] = board;
    Surface_setBoardCount(surface, count + 1);
}

// Removes the first matching borrowed board while preserving the order of remaining boards.
fn Surface_removeBoard(Surface *surface, Board *board) {
    if (!surface || !board) return;
    Board **boards = Surface_boards(surface);
    i32 count = Surface_boardCount(surface);
    for (i32 i = 0; i < count; i++) {
        if (boards[i] != board) continue;
        memmove(&boards[i], &boards[i + 1],
                (bytes)(count - i - 1) * sizeof *boards);
        Surface_setBoardCount(surface, count - 1);
        return;
    }
}

// The one call a Frame makes: revalidate each board (which renders its
// scene/content), then present the finished image to the host.
// Marks output pending and polls it, revalidating boards only when presentation is due.
fn Surface_revalidate(Surface *surface) {
    if (!surface) return;
    Surface_setPending(surface, true);
    Surface_poll(surface);
}

// Revalidates and presents pending output once its configured cap allows it.
fn Surface_poll(Surface *surface) {
    if (!surface || !Surface_pending(surface) || !surfaceDue(surface)) return;
    Surface_revalidateNow(surface);
}

// Revalidates each attached board in order, then attempts one presentation.
fn Surface_revalidateNow(Surface *surface) {
    if (!surface) return;
    Board **boards = Surface_boards(surface);
    i32 count = Surface_boardCount(surface);
    for (i32 i = 0; i < count; i++)
        Board_revalidate(boards[i]);
    Surface_present(surface);
}
