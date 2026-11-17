#include "vex.h"
#include "lang/board.h"

#include <stdlib.h>


;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Board (board.c)
 * ============================================================================
 * A RETAINED offscreen render target — NOT a swapchain and never a window
 * surface. You render into its Image, then composite it into the current
 * target. The board owns its pixels across frames; a resize recreates them only
 * when the extent actually changes.
 *
 * Board_publish() bumps an atomic generation, and that newer generation is what
 * wakes the present-on-demand loop, so a board that did not change costs zero
 * frames. To keep R3 free of R4 types, the board owns no scene: the layer that
 * DOES own the scene registers a revalidate step (BoardRevalidateFn), and
 * Board_revalidate runs them in order and then publishes. This is the middle of
 * the Frame -> Surface -> Board -> panel render cascade.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Board (board.c)
 * ============================================================================
 * Retained offscreen Image + atomic generation + borrowed revalidate steps.
 *
 * STRUCT FIELDS:
 * ----------------------------------------------------------------------------
 *   Image    *image;         // owned target (RGBA8)
 *   _Atomic u64 generation; // publishes so far (0 = never)
 *   any native;        // opaque dialect handle (the backend's VkImage)
 *   BoardRevalidateFn *revalidators; // borrowed steps (R4 scene/content)
 *   any *revalidateUd;  // userdata per step
 *   i32       revalidateCount, revalidateCap;
 *
 * FUNCTION REGISTRY (exported by board.h):
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Board_0, Board_2, Board_new, Board_destroy
 * Core:
 *   - Board_resize, Board_publish, Board_fill
 * Queries / Getters:
 *   - Board_width, Board_height, Board_generation, Board_isValid,
 *     Board_image, Board_native
 * Revalidation:
 *   - Board_addRevalidator, Board_clearRevalidators, Board_revalidate
 * ============================================================================
 */

struct Board {
    Image *image;
    _Atomic u64 generation;
    any native;    // opaque dialect handle (the backend's VkImage)
    BoardRevalidateFn *revalidators;   // borrowed steps (R4 scene/content panels)
    any *revalidateUd;               // userdata per step
    i32 revalidateCount, revalidateCap;
};

;;SETTER
static inline fn Board_setImage(Board *self, Image *v) { (*self).image = v; }
;;GETTER
static inline _Atomic u64 *Board_generationMut(Board *self) { return &(*self).generation; }
;;GETTER
static inline BoardRevalidateFn *Board_revalidators(const Board *self) { return (*self).revalidators; }
;;SETTER
static inline fn Board_setRevalidators(Board *self, BoardRevalidateFn *v) { (*self).revalidators = v; }
;;GETTER
static inline any *Board_revalidateUd(const Board *self) { return (*self).revalidateUd; }
;;SETTER
static inline fn Board_setRevalidateUd(Board *self, any *v) { (*self).revalidateUd = v; }
;;GETTER
static inline i32 Board_revalidateCount(const Board *self) { return (*self).revalidateCount; }
;;SETTER
static inline fn Board_setRevalidateCount(Board *self, i32 v) { (*self).revalidateCount = v; }
;;GETTER
static inline i32 Board_revalidateCap(const Board *self) { return (*self).revalidateCap; }
;;SETTER
static inline fn Board_setRevalidateCap(Board *self, i32 v) { (*self).revalidateCap = v; }

// Creates a retained RGBA image target from the descriptor, using 1x1 defaults when absent.
Board *Board_new(const BoardDesc *desc) {
    Board *b = calloc(1, sizeof *b);
    if (!b) return nullptr;
    BoardDesc d = desc ? *desc : (BoardDesc){1, 1, IMAGE_USAGE_RENDER | IMAGE_USAGE_SAMPLED | IMAGE_USAGE_TRANSFER};
    ImageDesc id = {d.width ? d.width : 1u, d.height ? d.height : 1u, IMAGE_FORMAT_RGBA8, d.usage};
    Image *image = Image_new(&id);
    if (!image) {
        free(b);
        return nullptr;
    }
    Board_setImage(b, image);
    return b;
}

// Creates a board with the default 1x1 image target.
Board *Board_0() { return Board_new(nullptr); }
// Creates a board with the requested dimensions and default image usage.
Board *Board_2(u32 width, u32 height) {
    BoardDesc d = {width, height, 0u};
    return Board_new(&d);
}

// Destroys the owned image and callback arrays; null is ignored.
fn Board_destroy(Board *board) {
    if (!board) return;
    Image_destroy(Board_image(board));
    free(Board_revalidators(board));
    free(Board_revalidateUd(board));
    free(board);
}

// Resizes the image target, preserving its allocation when the current capacity fits.
bool Board_resize(Board *board, u32 width, u32 height) {
    Image *image = Board_image(board);
    if (!board || !image) return false;
    if (Image_width(image) == width && Image_height(image) == height) return true;
    return Image_resize(image, width, height);
}

// Advances the atomic generation to announce newly published board content.
fn Board_publish(Board *board) {
    if (!board) return;
    atomic_fetch_add(Board_generationMut(board), 1u);
}

// Fills the board's image with the supplied packed color when the board is valid.
fn Board_fill(Board *board, Color color) {
    if (board && Board_image(board)) Image_fill(Board_image(board), color);
}

// Returns the image width, or zero when the board or image is absent.
u32 Board_width(const Board *board) { return board && Board_image(board) ? Image_width(Board_image(board)) : 0u; }
// Returns the image height, or zero when the board or image is absent.
u32 Board_height(const Board *board) { return board && Board_image(board) ? Image_height(Board_image(board)) : 0u; }
// Reads the atomic publish generation, or zero for a null board.
u64 Board_generation(const Board *board) {
    return board ? atomic_load(&(*board).generation) : 0u;
}
// Reports whether the board has a nonempty image target.
bool Board_isValid(const Board *board) { return board && Image_isValid(Board_image(board)); }
// Returns the board-owned image as a borrowed pointer, or nullptr for a null board.
Image *Board_image(const Board *board) { return board ? (*board).image : nullptr; }
// Returns the opaque native handle without transferring ownership.
any Board_native(const Board *board) { return board ? (*board).native : nullptr; }

// ── revalidation ────────────────────────────────────────────────────────────
// Appends a borrowed callback and userdata pair; invalid input or growth failure leaves it unadded.
fn Board_addRevalidator(Board *board, BoardRevalidateFn fn, any userdata) {
    if (!board || !fn) return;
    if (Board_revalidateCount(board) == Board_revalidateCap(board)) {
        i32 cap = Board_revalidateCap(board) ? Board_revalidateCap(board) * 2 : 4;
        BoardRevalidateFn *steps = realloc(Board_revalidators(board), (bytes)cap * sizeof *steps);
        any *uds = realloc(Board_revalidateUd(board), (bytes)cap * sizeof *uds);
        if (!steps || !uds) return;   // best-effort; keep whatever we had
        Board_setRevalidators(board, steps);
        Board_setRevalidateUd(board, uds);
        Board_setRevalidateCap(board, cap);
    }
    i32 count = Board_revalidateCount(board);
    Board_revalidators(board)[count] = fn;
    Board_revalidateUd(board)[count] = userdata;
    Board_setRevalidateCount(board, count + 1);
}

// Removes all registered callbacks without freeing their borrowed userdata.
fn Board_clearRevalidators(Board *board) {
    if (!board) return;
    Board_setRevalidateCount(board, 0);
}

// Runs registered callbacks in order, then publishes the board once.
fn Board_revalidate(Board *board) {
    if (!board) return;
    BoardRevalidateFn *revalidators = Board_revalidators(board);
    any *uds = Board_revalidateUd(board);
    i32 count = Board_revalidateCount(board);
    for (i32 i = 0; i < count; i++)
        revalidators[i](board, uds[i]);
    Board_publish(board);   // the board changed; wake the demand loop
}
