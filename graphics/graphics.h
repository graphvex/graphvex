#ifndef GRAPHICS_GRAPHICS_H
#define GRAPHICS_GRAPHICS_H

#include "vex.h"

// graphvex R3 — graphics/graphics.h
//
// THE CLEAN CORE. The UI is rectangles, so the drawable vocabulary is:
//
//     fill rect   •  image rect  •  text rect   (+ clip push/pop, + clear)
//
// There is NO drawRect, NO circles, NO paths, NO strokes. A rect with a border
// is a rect with a border; a "circle" is a rounded rect; a "stroke" is a border.
// Everything is one by-value Rect, batched into ONE forward render pass.
//
// Two layers:
//   1. DisplayList — the retained quad list the UI paint pass fills ("the
//      graphics language"). Pure data, no GPU, fully testable headless.
//   2. Backend     — a function-pointer row that DRAINS a list into pixels.
//      The raster backend is the headless test backend; the GPU backend lands
//      behind the SAME row (callers never touch a backend type).
//
// Laws honoured: Strict 0xRRGGBBAA Color Law (monotonic shifts below), Native
// Pixel Law (every rect is native hardware pixels; NDC exists only in shaders),
// Forward Rendering & Bounded Surface Law (viewport-bounded, scissored forward).

// ── geometry ────────────────────────────────────────────────────────────────
// Native pixels, Y-down. Mirrors vexspoke's R2 Rectangle (which becomes the
// shared shape once graphvex links R2). By value: a quad list cannot afford a
// heap hop per rect.
typedef struct Rect {
    f32 x;
    f32 y;
    f32 w;
    f32 h;
} Rect;

static inline bool Rect_isEmpty(const Rect r) { return r.w <= 0.0f || r.h <= 0.0f; }
static inline bool Rect_contains(const Rect r, f32 px, f32 py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}
static inline Rect Rect_intersect(const Rect a, const Rect b) {
    f32 x0 = a.x > b.x ? a.x : b.x;
    f32 y0 = a.y > b.y ? a.y : b.y;
    f32 x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    f32 y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    Rect r = {x0, y0, x1 - x0, y1 - y0};
    return r;
}

// ── color (Strict 0xRRGGBBAA Color Law) ─────────────────────────────────────
typedef u32 Color;   // 0xRRGGBBAA
#define COLOR_BLACK       0x000000FFu
#define COLOR_WHITE       0xFFFFFFFFu
#define COLOR_CLEAR       0x00000000u
#define COLOR_RGBA(r, g, b, a) \
    ((((u32)(r) & 0xFFu) << 24) | (((u32)(g) & 0xFFu) << 16) | \
     (((u32)(b) & 0xFFu) << 8) | ((u32)(a) & 0xFFu))

static inline u8 Color_red(const Color c) { return (u8)((c >> 24) & 0xFFu); }
static inline u8 Color_green(const Color c) { return (u8)((c >> 16) & 0xFFu); }
static inline u8 Color_blue(const Color c) { return (u8)((c >> 8) & 0xFFu); }
static inline u8 Color_alpha(const Color c) { return (u8)(c & 0xFFu); }

// ── brush: color + optional rounded corners + optional border ───────────────
typedef struct Brush {
    Color color;
    f32 radius;       // corner radius in native px; 0 = square
    Color border;     // border color (alpha 0 = none)
    f32 borderWidth;  // native px; 0 = none
    f32 blur;         // soft-edge falloff in px; 0 = hard (AA only)
} Brush;

// ── the drawable Image (pixel buffer; defined in image.h) ───────────────────
typedef struct Image Image;

// ── the display list ────────────────────────────────────────────────────────
enum { CMD_RECT = 0, CMD_IMAGE, CMD_TEXT, CMD_CLIP_PUSH, CMD_CLIP_POP };

typedef struct DrawCmd {
    i32 kind;
    Rect dst;         // destination rect (native px)
    Rect src;         // source rect for images (native px); zero for rects
    Color color;      // fill / text color
    f32 radius;
    f32 border;
    Color borderColor;
    f32 blur;       // soft-edge falloff (px); 0 = hard
    const Image *image;   // borrowed drawable image (null for rects/text)
    const char *text;   // borrowed; NUL-terminated (never freed by the list)
} DrawCmd;

typedef struct DisplayList DisplayList;

DisplayList *DisplayList_0();
#define DisplayList(...) CONSTRUCTOR_DISPATCH(DisplayList, __VA_ARGS__)
fn DisplayList_free(DisplayList *dl);
fn DisplayList_clear(DisplayList *dl);
fn DisplayList_rect(DisplayList *dl, Rect dst, const Brush *brush);
fn DisplayList_image(DisplayList *dl, const Image *image, Rect src, Rect dst);
fn DisplayList_text(DisplayList *dl, Rect dst, const char *text, Color color);
fn DisplayList_clip(DisplayList *dl, Rect rect);
fn DisplayList_clipRounded(DisplayList *dl, Rect rect, f32 radius);   // rounded mask
fn DisplayList_unclip(DisplayList *dl);
bytes DisplayList_count(const DisplayList *dl);
const DrawCmd *DisplayList_cmds(const DisplayList *dl);

// ── the backend row (the slim seam) ─────────────────────────────────────────
typedef struct Backend {
    u32 id;
    bool (*begin)();
    bool (*end)();
    bool (*present)();
    bool (*resize)(u32 width, u32 height);
    bool (*clear)(Color color);
    bool (*clip)(const Rect *rect, f32 radius);   // null = reset; radius 0 = rect
    bool (*fillRect)(const Rect *rect, const Brush *brush);
    bool (*drawImage)(const Image *image, const Rect *dst);
    bool (*drawText)(const Rect *rect, const char *text, const Brush *brush);
    bool (*capture)(Image *dest);   // screenshot: copy the current target into dest
} Backend;

// ── core ────────────────────────────────────────────────────────────────────
bool Graphics_register(const Backend *row);
bool Graphics_use(u32 backendId);
u32 Graphics_backendId();
const Backend *Graphics_current();

bool Graphics_begin();
bool Graphics_end();
bool Graphics_present();
bool Graphics_resize(u32 width, u32 height);
bool Graphics_clear(Color color);
bool Graphics_clip(const Rect *rect);
bool Graphics_clipRounded(const Rect *rect, f32 radius);   // rectangular clip when radius <= 0
bool Graphics_fillRect(const Rect *rect, const Brush *brush);
bool Graphics_drawImage(const Image *image, const Rect *dst);
bool Graphics_drawText(const Rect *rect, const char *text, const Brush *brush);

// Screenshot: copy the current render target's pixels (RGBA8, 0xRRGGBBAA byte
// order) into `dest`, sizing it to the target. Backend-agnostic — the raster
// backend copies its framebuffer; a GPU backend does a readback. This is what
// a CAPTURE() test uses to grab a frame for a human or an agent to inspect.
bool Graphics_capture(Image *dest);

// CAPTURE(&image) — screenshot shortcut for tests/harnesses.
#define CAPTURE(dest) Graphics_capture(dest)

// Stream an entire list through the active backend (the One Paint Path).
bool Graphics_submit(DisplayList *dl);

// ── the headless CPU backend (tests / CI) ───────────────────────────────────
#define BACKEND_NONE    0u
#define BACKEND_RASTER  1u

const Backend *RasterGraphics_row();
bool Raster_configure(u32 width, u32 height);   // (re)allocate the target
const u32 *Raster_pixels();                      // 0xRRGGBBAA, tightly packed
u32 Raster_pixelAt(u32 x, u32 y);          // 0 when out of bounds

#endif // GRAPHICS_GRAPHICS_H
