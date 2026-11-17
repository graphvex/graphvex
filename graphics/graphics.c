#include "vex.h"
#include "graphics/graphics.h"
#include "graphics/image_runs.h"

#include "lang/image.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/** This module implements the backend-neutral drawing forwarders, retained
 * DisplayList recording/submission, and the headless CPU raster Backend row.
 * It is a procedural integration unit rather than one class file: Graphvex
 * graphics state and the Raster reference backend share this translation unit
 * today. Submission applies nested clips without resizing geometry and restores
 * the caller's base clip. This co-location is documented, not a waiver of the
 * Single Class Per File Law; extraction remains required before treating these
 * behavior owners as one class. */
;;OVERVIEW
/** ========================================================================
 * MODULE: graphics/graphics.c
 * MODULE STATE
 * const Backend *s_rows[8]     registered callback rows
 * i32 s_rowCount               initialized backend rows
 * const Backend *s_current     selected backend row
 * DrawCmd s_baseClip           external clip scope
 * bool s_hasBaseClip           whether the external clip is active
 * u32 *s_px                raster pixel backing
 * i32 s_capW,s_capH        allocated raster extent/row stride
 * i32 s_w,s_h              current raster viewport extent
 * Rect s_clip; f32 s_clipRadius active raster clipping geometry
 * FUNCTION REGISTRY
 * Backend registry: Graphics_register, Graphics_use, Graphics_backendId,
 *   Graphics_current
 * Backend forwarding: Graphics_begin/end/present/resize/clear/clip/
 *   clipRounded/fillRect/drawImage/drawText/capture
 * DisplayList: DisplayList_0/free/clear/rect/image/text/clip/clipRounded/
 *   unclip/count/cmds
 * Submission: Graphics_submit
 * Raster backend: RasterGraphics_row, Raster_configure, Raster_pixels,
 *   Raster_pixelAt
 * Private behavior: dl_push grows command storage; submit_draw dispatches one
 *   draw; submit_clip and submit_mask read clip state; submit_span,
 *   submit_masks, and submit_masked geometry apply rectangular/rounded scopes;
 *   blend_over performs RGBA compositing; raster_begin/end/present/resize/
 *   clear/clip/fillRect/imageRun/drawImage/drawText/capture implement the CPU
 *   backend; sd_round_box computes rounded-rectangle coverage.
 * Ownership and failure contract:
 * DisplayList owns command and clip-stack allocations, but command text/images
 * are borrowed through submission. Raster backing is module-owned and grow-only.
 * Registry capacity is currently eight and a full registry refuses admission;
 * submit failure restores the external clip when the backend callback permits.
 * The API has global state and requires caller serialization.
 * ======================================================================== */

// ── backend registry ────────────────────────────────────────────────────────
#define MAX_BACKENDS 8
static const Backend *s_rows[MAX_BACKENDS];
static i32 s_rowCount = 0;
static const Backend *s_current = nullptr;
static DrawCmd s_baseClip;
static bool s_hasBaseClip = false; // explicit clip outside a display-list scope

const Backend *RasterGraphics_row();   // defined below

;;GETTER
static u32 Backend_id(const Backend *b) { return (*b).id; }
;;GETTER
static bool Backend_callBegin(const Backend *b) { return (*b).begin ? (*b).begin() : false; }
;;GETTER
static bool Backend_callEnd(const Backend *b) { return (*b).end ? (*b).end() : false; }
;;GETTER
static bool Backend_callPresent(const Backend *b) { return (*b).present ? (*b).present() : false; }
;;GETTER
static bool Backend_callResize(const Backend *b, u32 w, u32 h) { return (*b).resize ? (*b).resize(w, h) : false; }
;;GETTER
static bool Backend_callClear(const Backend *b, Color c) { return (*b).clear ? (*b).clear(c) : false; }
;;GETTER
static bool Backend_callClip(const Backend *b, const Rect *r, f32 rad) { return (*b).clip ? (*b).clip(r, rad) : false; }
;;GETTER
static bool Backend_hasClip(const Backend *b) { return (*b).clip != nullptr; }
;;GETTER
static bool Backend_callFillRect(const Backend *b, const Rect *r, const Brush *br) { return (*b).fillRect ? (*b).fillRect(r, br) : false; }
;;GETTER
static bool Backend_callDrawImage(const Backend *b, const Image *img, const Rect *dst) { return (*b).drawImage ? (*b).drawImage(img, dst) : false; }
;;GETTER
static bool Backend_callDrawText(const Backend *b, const Rect *r, const char *t, const Brush *br) { return (*b).drawText ? (*b).drawText(r, t, br) : false; }
;;GETTER
static bool Backend_callCapture(const Backend *b, Image *dest) { return (*b).capture ? (*b).capture(dest) : false; }

// Registers or replaces a backend descriptor by ID.
bool Graphics_register(const Backend *row) {
    if (!row || Backend_id(row) == BACKEND_NONE) return false;
    for (i32 i = 0; i < s_rowCount; i++) {
        if (Backend_id(s_rows[i]) == Backend_id(row)) { s_rows[i] = row; return true; }
    }
    if (s_rowCount >= MAX_BACKENDS) return false;
    s_rows[s_rowCount++] = row;
    return true;
}

// Selects a registered backend and configures its callbacks for current dimensions.
bool Graphics_use(u32 backendId) {
    for (i32 i = 0; i < s_rowCount; i++) {
        if (Backend_id(s_rows[i]) == backendId) { s_current = s_rows[i]; s_hasBaseClip = false; return true; }
    }
    // convenience: the raster backend self-registers on first use
    if (backendId == BACKEND_RASTER) {
        Graphics_register(RasterGraphics_row());
        for (i32 i = 0; i < s_rowCount; i++)
            if (Backend_id(s_rows[i]) == backendId) { s_current = s_rows[i]; s_hasBaseClip = false; return true; }
    }
    return false;
}

// Returns the active backend ID, or BACKEND_NONE when no backend is selected.
u32 Graphics_backendId() { return s_current ? Backend_id(s_current) : BACKEND_NONE; }
const Backend *Graphics_current() { return s_current; }

// ── forwarders (call through the active row; false when none) ───────────────
bool Graphics_begin()   { return s_current ? Backend_callBegin(s_current) : false; }
// Forwards frame completion to the active backend when provided.
bool Graphics_end()     { return s_current ? Backend_callEnd(s_current) : false; }
// Forwards presentation to the active backend when provided.
bool Graphics_present() { return s_current ? Backend_callPresent(s_current) : false; }
// Resizes the active backend and updates graphics clip state on success.
bool Graphics_resize(u32 w, u32 h) {
    bool ok = s_current ? Backend_callResize(s_current, w, h) : false;
    if (ok) s_hasBaseClip = false;
    return ok;
}
// Clears the active backend with color when its clear callback exists.
bool Graphics_clear(Color color) {
    return s_current ? Backend_callClear(s_current, color) : false;
}
// Installs the base rounded clip used by subsequent drawing operations.
bool Graphics_clipRounded(const Rect *rect, f32 radius) {
    if (!s_current || !Backend_callClip(s_current, rect, radius)) return false;
    s_hasBaseClip = rect != nullptr;
    if (rect) { s_baseClip.dst = *rect; s_baseClip.radius = fmaxf(radius, 0); }
    return true;
}
// Installs a rectangular base clip by using a zero corner radius.
bool Graphics_clip(const Rect *rect) { return Graphics_clipRounded(rect, 0.0f); }
// Forwards a rectangle fill to the active backend.
bool Graphics_fillRect(const Rect *rect, const Brush *brush) {
    return s_current ? Backend_callFillRect(s_current, rect, brush) : false;
}
// Forwards an image draw to the active backend.
bool Graphics_drawImage(const Image *image, const Rect *dst) {
    return s_current ? Backend_callDrawImage(s_current, image, dst) : false;
}
// Forwards a text draw to the active backend.
bool Graphics_drawText(const Rect *rect, const char *text, const Brush *brush) {
    return s_current ? Backend_callDrawText(s_current, rect, text, brush) : false;
}
// Captures the active backend into dest when its capture callback is available.
bool Graphics_capture(Image *dest) {
    return s_current ? Backend_callCapture(s_current, dest) : false;
}

// ── the display list ────────────────────────────────────────────────────────
struct DisplayList {
    DrawCmd *items;
    bytes count;
    bytes cap;
    bytes *clipStack;       // command indices; reusable submission scratch
    bytes clipCap, clipDepth; // reserved nesting depth and recording depth
};

;;GETTER
static DrawCmd *DisplayList_items(const DisplayList *dl) { return (*dl).items; }
;;SETTER
static fn DisplayList_setItems(DisplayList *dl, DrawCmd *items) { (*dl).items = items; }
;;GETTER
static bytes DisplayList_rawCount(const DisplayList *dl) { return (*dl).count; }
;;SETTER
static fn DisplayList_setCount(DisplayList *dl, bytes count) { (*dl).count = count; }
;;GETTER
static bytes DisplayList_cap(const DisplayList *dl) { return (*dl).cap; }
;;SETTER
static fn DisplayList_setCap(DisplayList *dl, bytes cap) { (*dl).cap = cap; }
;;GETTER
static bytes *DisplayList_clipStack(const DisplayList *dl) { return (*dl).clipStack; }
;;SETTER
static fn DisplayList_setClipStack(DisplayList *dl, bytes *stack) { (*dl).clipStack = stack; }
;;GETTER
static bytes DisplayList_clipCap(const DisplayList *dl) { return (*dl).clipCap; }
;;SETTER
static fn DisplayList_setClipCap(DisplayList *dl, bytes cap) { (*dl).clipCap = cap; }
;;GETTER
static bytes DisplayList_clipDepth(const DisplayList *dl) { return (*dl).clipDepth; }
;;SETTER
static fn DisplayList_setClipDepth(DisplayList *dl, bytes depth) { (*dl).clipDepth = depth; }

;;GETTER
static i32 DrawCmd_kind(const DrawCmd *c) { return (*c).kind; }
;;SETTER
static fn DrawCmd_setKind(DrawCmd *c, i32 kind) { (*c).kind = kind; }
;;GETTER
static Rect DrawCmd_dst(const DrawCmd *c) { return (*c).dst; }
;;SETTER
static fn DrawCmd_setDst(DrawCmd *c, Rect dst) { (*c).dst = dst; }
;;GETTER
static Rect DrawCmd_src(const DrawCmd *c) { return (*c).src; }
;;SETTER
static fn DrawCmd_setSrc(DrawCmd *c, Rect src) { (*c).src = src; }
;;GETTER
static Color DrawCmd_color(const DrawCmd *c) { return (*c).color; }
;;SETTER
static fn DrawCmd_setColor(DrawCmd *c, Color color) { (*c).color = color; }
;;GETTER
static f32 DrawCmd_radius(const DrawCmd *c) { return (*c).radius; }
;;SETTER
static fn DrawCmd_setRadius(DrawCmd *c, f32 radius) { (*c).radius = radius; }
;;GETTER
static Color DrawCmd_borderColor(const DrawCmd *c) { return (*c).borderColor; }
;;SETTER
static fn DrawCmd_setBorderColor(DrawCmd *c, Color color) { (*c).borderColor = color; }
;;GETTER
static f32 DrawCmd_border(const DrawCmd *c) { return (*c).border; }
;;SETTER
static fn DrawCmd_setBorder(DrawCmd *c, f32 border) { (*c).border = border; }
;;GETTER
static f32 DrawCmd_blur(const DrawCmd *c) { return (*c).blur; }
;;SETTER
static fn DrawCmd_setBlur(DrawCmd *c, f32 blur) { (*c).blur = blur; }
;;GETTER
static const Image *DrawCmd_image(const DrawCmd *c) { return (*c).image; }
;;SETTER
static fn DrawCmd_setImage(DrawCmd *c, const Image *img) { (*c).image = img; }
;;GETTER
static const char *DrawCmd_text(const DrawCmd *c) { return (*c).text; }
;;SETTER
static fn DrawCmd_setText(DrawCmd *c, const char *text) { (*c).text = text; }

;;GETTER
static Color Brush_color(const Brush *b) { return (*b).color; }
;;GETTER
static f32 Brush_radius(const Brush *b) { return (*b).radius; }
;;GETTER
static Color Brush_border(const Brush *b) { return (*b).border; }
;;GETTER
static f32 Brush_borderWidth(const Brush *b) { return (*b).borderWidth; }
;;GETTER
static f32 Brush_blur(const Brush *b) { return (*b).blur; }

;;GETTER
static f32 Rect_x(const Rect *r) { return (*r).x; }
;;GETTER
static f32 Rect_y(const Rect *r) { return (*r).y; }
;;GETTER
static f32 Rect_w(const Rect *r) { return (*r).w; }
;;GETTER
static f32 Rect_h(const Rect *r) { return (*r).h; }

DisplayList *DisplayList_0() {
    DisplayList *dl = calloc(1, sizeof *dl);
    return dl;
}

// Frees the command array and display-list record.
fn DisplayList_free(DisplayList *dl) {
    if (!dl) return;
    free(DisplayList_items(dl));
    free(DisplayList_clipStack(dl));
    free(dl);
}

// Resets the command count and clip stack while retaining allocated capacity.
fn DisplayList_clear(DisplayList *dl) {
    if (dl) { DisplayList_setCount(dl, 0); DisplayList_setClipDepth(dl, 0); }
}

static DrawCmd *dl_push(DisplayList *dl) {
    if (DisplayList_rawCount(dl) == DisplayList_cap(dl)) {
        bytes cap = DisplayList_cap(dl) ? DisplayList_cap(dl) * 2 : 64;
        DrawCmd *items = realloc(DisplayList_items(dl), cap * sizeof *items);
        if (!items) { DisplayList_setCap(dl, 0); DisplayList_setCount(dl, 0); return nullptr; }
        DisplayList_setItems(dl, items);
        DisplayList_setCap(dl, cap);
    }
    bytes idx = DisplayList_rawCount(dl);
    DisplayList_setCount(dl, idx + 1);
    DrawCmd *c = &DisplayList_items(dl)[idx];
    memset(c, 0, sizeof *c);
    return c;
}

// Appends a rectangle draw command with a copied brush value.
fn DisplayList_rect(DisplayList *dl, Rect dst, const Brush *brush) {
    if (!dl || !brush || Rect_isEmpty(dst)) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    DrawCmd_setKind(c, CMD_RECT);
    DrawCmd_setDst(c, dst);
    DrawCmd_setColor(c, Brush_color(brush));
    DrawCmd_setRadius(c, Brush_radius(brush));
    DrawCmd_setBorderColor(c, Brush_border(brush));
    DrawCmd_setBorder(c, Brush_borderWidth(brush));
    DrawCmd_setBlur(c, Brush_blur(brush));
}

// Appends an image draw command and retains its image for deferred submission.
fn DisplayList_image(DisplayList *dl, const Image *image, Rect src, Rect dst) {
    if (!dl || !image || Rect_isEmpty(dst)) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    DrawCmd_setKind(c, CMD_IMAGE);
    DrawCmd_setDst(c, dst);
    DrawCmd_setSrc(c, src);
    DrawCmd_setImage(c, image);
}

// Appends a text command with copied text and color data.
fn DisplayList_text(DisplayList *dl, Rect dst, const char *text, Color color) {
    if (!dl || !text || Rect_isEmpty(dst)) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    DrawCmd_setKind(c, CMD_TEXT);
    DrawCmd_setDst(c, dst);
    DrawCmd_setText(c, text);
    DrawCmd_setColor(c, color);
}

// Pushes a rectangular clip command and records its stack entry.
fn DisplayList_clip(DisplayList *dl, Rect rect) {
    DisplayList_clipRounded(dl, rect, 0.0f);
}

// Pushes a rounded clip command and records its stack entry.
fn DisplayList_clipRounded(DisplayList *dl, Rect rect, f32 radius) {
    if (!dl) return;
    if (DisplayList_clipDepth(dl) == DisplayList_clipCap(dl)) {
        bytes cap = DisplayList_clipCap(dl) ? DisplayList_clipCap(dl) * 2 : 8;
        bytes *grown = realloc(DisplayList_clipStack(dl), cap * sizeof *grown);
        if (!grown) return;
        DisplayList_setClipStack(dl, grown);
        DisplayList_setClipCap(dl, cap);
    }
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    DisplayList_setClipDepth(dl, DisplayList_clipDepth(dl) + 1);
    DrawCmd_setKind(c, CMD_CLIP_PUSH);
    DrawCmd_setDst(c, rect);
    DrawCmd_setRadius(c, radius > 0.0f ? radius : 0.0f);   // 0 = rectangular scissor
}

// Pops one nested clip when present and appends the matching unclip command.
fn DisplayList_unclip(DisplayList *dl) {
    if (!dl) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    DrawCmd_setKind(c, CMD_CLIP_POP);
    if (DisplayList_clipDepth(dl) > 0) DisplayList_setClipDepth(dl, DisplayList_clipDepth(dl) - 1);
}

// Returns the number of recorded commands, or zero for null.
bytes DisplayList_count(const DisplayList *dl) { return dl ? DisplayList_rawCount(dl) : 0; }
const DrawCmd *DisplayList_cmds(const DisplayList *dl) { return dl ? DisplayList_items(dl) : nullptr; }

// Submit original geometry; scissors must never resize rounded boxes or UVs.
static bool submit_draw(const DrawCmd *cmd) {
    Brush brush = {DrawCmd_color(cmd), DrawCmd_radius(cmd), DrawCmd_borderColor(cmd), DrawCmd_border(cmd), DrawCmd_blur(cmd)};
    Rect dst = DrawCmd_dst(cmd);
    switch (DrawCmd_kind(cmd)) {
        case CMD_RECT: return Graphics_fillRect(&dst, &brush);
        case CMD_IMAGE: return Graphics_drawImage(DrawCmd_image(cmd), &dst);
        case CMD_TEXT: return Graphics_drawText(&dst, DrawCmd_text(cmd), &brush);
        default: return false;
    }
}

static bool submit_clip(const Rect *rect, f32 radius) {
    return Backend_hasClip(s_current) && Backend_callClip(s_current, rect, radius);
}

static const DrawCmd *submit_mask(DisplayList *dl, bytes index) {
    if (s_hasBaseClip) {
        if (index == 0) return &s_baseClip;
        --index;
    }
    return &DisplayList_items(dl)[DisplayList_clipStack(dl)[index]];
}

static bool submit_span(const DrawCmd *cmd, Rect span) {
    return Rect_isEmpty(span) || (submit_clip(&span, 0) && submit_draw(cmd));
}

static bool submit_masks(DisplayList *dl, const DrawCmd *cmd, bytes depth) {
    depth += s_hasBaseClip ? 1 : 0;
    if (depth == 0)
        return submit_clip(nullptr, 0) && submit_draw(cmd);
    if (depth == 1) {
        const DrawCmd *mask = submit_mask(dl, 0);
        Rect maskDst = DrawCmd_dst(mask);
        return submit_clip(&maskDst, DrawCmd_radius(mask)) && submit_draw(cmd);
    }
    Rect bounds = DrawCmd_dst(cmd);
    bool rounded = false;
    for (bytes i = 0; i < depth; ++i) {
        const DrawCmd *mask = submit_mask(dl, i);
        bounds = Rect_intersect(bounds, DrawCmd_dst(mask));
        rounded |= DrawCmd_radius(mask) > 0.0f;
    }
    if (Rect_isEmpty(bounds)) return true;
    if (!rounded) return submit_clip(&bounds, 0) && submit_draw(cmd);
    if (!isfinite(bounds.x) || !isfinite(bounds.y) ||
        !isfinite(bounds.w) || !isfinite(bounds.h)) return false;

    // Rounded rectangles are convex: intersect the horizontal interval each
    // ancestor admits at the pixel center. Coalesce identical adjacent rows.
    // Cost is restricted to this quad's clipped bounds, not the whole window.
    Rect span = {0};
    f32 end = ceilf(bounds.y + bounds.h - 0.5f);
    for (f32 y = ceilf(bounds.y - 0.5f); y < end; y += 1.0f) {
        f32 left = bounds.x, right = bounds.x + bounds.w;
        for (bytes i = 0; i < depth; ++i) {
            const DrawCmd *mask = submit_mask(dl, i);
            Rect r = DrawCmd_dst(mask);
            f32 radius = fminf(DrawCmd_radius(mask), fminf(r.w, r.h) * 0.5f);
            if (radius <= 0.0f) continue;
            f32 qy = fabsf(y + 0.5f - r.y - r.h * 0.5f) - (r.h * 0.5f - radius);
            f32 inset = qy > 0.0f ? radius - sqrtf(fmaxf(0.0f, radius * radius - qy * qy)) : 0.0f;
            left = fmaxf(left, r.x + inset);
            right = fminf(right, r.x + r.w - inset);
        }
        f32 x0 = ceilf(left - 0.5f), x1 = ceilf(right - 0.5f);
        if (x1 > x0 && span.h > 0 && span.x == x0 && span.w == x1 - x0 && span.y + span.h == y) {
            span.h += 1;
        } else {
            if (!submit_span(cmd, span)) return false;
            span = (Rect){x0, y, fmaxf(x1 - x0, 0), 1};
        }
    }
    return submit_span(cmd, span);
}

// Submits recorded commands in order while applying accumulated clip masks.
bool Graphics_submit(DisplayList *dl) {
    if (!dl || !s_current) return false;
    bytes depth = 0;
    bool ok = true;
    for (bytes i = 0; i < DisplayList_rawCount(dl) && ok; ++i) {
        const DrawCmd *cmd = &DisplayList_items(dl)[i];
        if (DrawCmd_kind(cmd) == CMD_CLIP_PUSH) {
            if (depth >= DisplayList_clipCap(dl)) { ok = false; break; }
            DisplayList_clipStack(dl)[depth++] = i;
        } else if (DrawCmd_kind(cmd) == CMD_CLIP_POP) {
            if (depth == 0) { ok = false; break; }
            --depth;
        } else {
            ok = submit_masks(dl, cmd, depth);
        }
    }
    // Restore the caller's explicit clip on success and error; list-local
    // masks never leak across submissions or overwrite an enclosing scope.
    bool reset = submit_clip(s_hasBaseClip ? &s_baseClip.dst : nullptr, s_baseClip.radius);
    return ok && depth == 0 && reset;
}

// ── headless CPU (raster) backend ───────────────────────────────────────────
static u32 *s_px = nullptr;
static i32 s_capW = 0, s_capH = 0;   // allocation; row stride == s_capW
static i32 s_w = 0, s_h = 0;         // logical viewport (the scissor region)
static Rect s_clip = {0, 0, 0, 0};
static f32 s_clipRadius = 0.0f;   // > 0 = rounded mask on the clip rect

static u32 blend_over(u32 dst, u32 src) {
    u32 sa = src & 0xFFu;
    if (sa == 255u) return src;
    if (sa == 0u) return dst;
    u32 ia = 255u - sa;
    u32 r = (((src >> 24) & 0xFFu) * sa + ((dst >> 24) & 0xFFu) * ia) / 255u;
    u32 g = (((src >> 16) & 0xFFu) * sa + ((dst >> 16) & 0xFFu) * ia) / 255u;
    u32 b = (((src >> 8) & 0xFFu) * sa + ((dst >> 8) & 0xFFu) * ia) / 255u;
    u32 a = (sa * 255u + ((dst & 0xFFu) * ia)) / 255u;
    return (r << 24) | (g << 16) | (b << 8) | a;
}

// Allocates or resizes the CPU reference framebuffer to the requested dimensions.
bool Raster_configure(u32 width, u32 height) {
    // GROW-ONLY. The framebuffer is allocated once and only ever grows, so a
    // resize is pure scissoring: no realloc, no refill, ever. The row stride is
    // the allocation width (s_capW), independent of the logical width (s_w).
    if (width == 0 || height == 0) {
        s_w = 0;
        s_h = 0;
        s_clip = (Rect){0, 0, 0, 0};
        return true;
    }
    if (s_px && width <= (u32)s_capW && height <= (u32)s_capH) {
        s_w = (i32)width;
        s_h = (i32)height;
        s_clip = (Rect){0, 0, (f32)width, (f32)height};
        return true;
    }
    u32 nw = s_capW > 0 ? (u32)s_capW : width;
    u32 nh = s_capH > 0 ? (u32)s_capH : height;
    while (nw < width) nw += nw / 2 + 64;   // amortized growth
    while (nh < height) nh += nh / 2 + 64;
    u32 *grown = realloc(s_px, (bytes)nw * (bytes)nh * sizeof *grown);
    if (!grown) return false;
    s_px = grown;
    s_capW = (i32)nw;
    s_capH = (i32)nh;
    s_w = (i32)width;
    s_h = (i32)height;
    s_clip = (Rect){0, 0, (f32)width, (f32)height};
    return true;
}

const u32 *Raster_pixels() { return s_px; }

// Returns the packed pixel at x,y, or transparent black outside the configured extent.
u32 Raster_pixelAt(u32 x, u32 y) {
    if (!s_px || (i32)x >= s_w || (i32)y >= s_h) return 0u;
    return s_px[(bytes)y * (bytes)s_capW + (bytes)x];
}

static bool raster_begin() { return s_px != nullptr; }
static bool raster_end() { return true; }
static bool raster_present() { return true; }

static bool raster_resize(u32 w, u32 h) { return Raster_configure(w, h); }

static bool raster_clear(Color color) {
    if (!s_px) return false;
    for (i32 y = 0; y < s_h; y++) {
        u32 *row = s_px + (bytes)y * (bytes)s_capW;
        for (i32 x = 0; x < s_w; x++) row[x] = color;
    }
    return true;
}

static bool raster_clip(const Rect *rect, f32 radius) {
    if (!rect) {
        s_clip = (Rect){0, 0, (f32)s_w, (f32)s_h};
        s_clipRadius = 0.0f;
        return true;
    }
    s_clip = *rect;
    s_clipRadius = radius > 0.0f ? radius : 0.0f;
    return true;
}

// signed distance to a rounded box centred at the origin (negative = inside)
static f32 sd_round_box(f32 px, f32 py, f32 hw, f32 hh, f32 r) {
    f32 qx = fabsf(px) - hw + r;
    f32 qy = fabsf(py) - hh + r;
    f32 mx = qx > 0.0f ? qx : 0.0f;
    f32 my = qy > 0.0f ? qy : 0.0f;
    f32 inside = qx > qy ? qx : qy;
    if (inside > 0.0f) inside = 0.0f;
    return inside + sqrtf(mx * mx + my * my) - r;
}

// The quad ALREADY includes the blur margin, so the SHAPE is the quad inset by
// `blur`; coverage falls off smoothly over `blur` px. blur == 0 keeps the sharp
// 1px-antialiased edge. The element's own rect is untouched — only the paint
// area grew.
static bool raster_fillRect(const Rect *rect, const Brush *brush) {
    if (!s_px || !rect || !brush) return false;
    f32 blur = Brush_blur(brush) > 0.0f ? Brush_blur(brush) : 0.0f;
    f32 cx = Rect_x(rect) + Rect_w(rect) * 0.5f;
    f32 cy = Rect_y(rect) + Rect_h(rect) * 0.5f;
    f32 hw = Rect_w(rect) * 0.5f - blur;
    f32 hh = Rect_h(rect) * 0.5f - blur;
    if (hw < 0.0f) hw = 0.0f;
    if (hh < 0.0f) hh = 0.0f;
    f32 r = Brush_radius(brush);
    f32 rmax = hw < hh ? hw : hh;
    if (r > rmax) r = rmax;
    if (r < 0.0f) r = 0.0f;

    Rect clipr = Rect_intersect(*rect, s_clip);
    if (Rect_isEmpty(clipr)) return true;
    i32 x0 = (i32)clipr.x < 0 ? 0 : (i32)clipr.x;
    i32 y0 = (i32)clipr.y < 0 ? 0 : (i32)clipr.y;
    i32 x1 = (i32)(clipr.x + clipr.w); if (x1 > s_w) x1 = s_w;
    i32 y1 = (i32)(clipr.y + clipr.h); if (y1 > s_h) y1 = s_h;

    for (i32 y = y0; y < y1; y++) {
        for (i32 x = x0; x < x1; x++) {
            if (s_clipRadius > 0.0f) {
                // the active clip may be a rounded mask (a parent's corner radius)
                f32 ccx = s_clip.x + s_clip.w * 0.5f;
                f32 ccy = s_clip.y + s_clip.h * 0.5f;
                f32 chw = s_clip.w * 0.5f, chh = s_clip.h * 0.5f;
                f32 cr = s_clipRadius;
                f32 cmax = chw < chh ? chw : chh;
                if (cr > cmax) cr = cmax;
                if (sd_round_box((f32)x + 0.5f - ccx, (f32)y + 0.5f - ccy, chw, chh, cr) > 0.0f)
                    continue;
            }
            f32 d = sd_round_box((f32)x + 0.5f - cx, (f32)y + 0.5f - cy, hw, hh, r);
            f32 a;
            if (blur > 0.0f) {
                // falloff centred on the shape edge: 1 inside, 0.5 at the edge,
                // 0 at +blur — a bleed, not a full-opacity ring. A wider blur
                // spreads the same panel over more pixels, so its colour weakens.
                if (d <= -blur) a = 1.0f;
                else if (d >= blur) continue;
                else { f32 t = (d + blur) / (2.0f * blur); a = 1.0f - t * t * (3.0f - 2.0f * t); }
                a *= 24.0f / (24.0f + blur);
            } else {
                a = 0.5f - d;
                if (a <= 0.0f) continue;
                if (a > 1.0f) a = 1.0f;
            }
            f32 stroke = Brush_borderWidth(brush) > 0.0f ? Brush_borderWidth(brush) : 0.0f;
            f32 borderMix = stroke > 0.0f ? fminf(fmaxf(d + stroke + 0.5f, 0.0f), 1.0f) : 0.0f;
            f32 borderAlpha = (f32) Color_alpha(Brush_border(brush)) / 255.0f * borderMix;
            Color fill = Brush_color(brush), border = Brush_border(brush);
            u32 red = (u32) ((f32) Color_red(fill) * (1 - borderAlpha) + (f32) Color_red(border) * borderAlpha + 0.5f);
            u32 green = (u32) ((f32) Color_green(fill) * (1 - borderAlpha) + (f32) Color_green(border) * borderAlpha + 0.5f);
            u32 blue = (u32) ((f32) Color_blue(fill) * (1 - borderAlpha) + (f32) Color_blue(border) * borderAlpha + 0.5f);
            u32 sa = (u32) (((f32) Color_alpha(fill) * (1 - borderAlpha) + (f32) Color_alpha(border) * borderAlpha) * a + 0.5f);
            if (sa == 0u) continue;
            u32 src = COLOR_RGBA(red, green, blue, sa);
            bytes i = (bytes)y * (bytes)s_capW + (bytes)x;
            s_px[i] = blend_over(s_px[i], src);
        }
    }
    return true;
}

static bool raster_imageRun(Rect run, Color color, any context) {
    (discard) context;
    Brush brush = {color, 0, 0, 0, 0};
    return raster_fillRect(&run, &brush);
}

static bool raster_drawImage(const Image *image, const Rect *dst) {
    if (!s_px || !dst)
        return false;
    Rect viewport = {0, 0, (f32) s_w, (f32) s_h};
    Rect clip = Rect_intersect(s_clip, viewport);
    if (Rect_isEmpty(clip))
        return true;
    return ImageRuns_visit(image, *dst, clip, raster_imageRun, nullptr);
}

static bool raster_drawText(const Rect *rect, const char *text, const Brush *brush) {
    // Text metrics/glyphs are a later slice; the core records intent only.
    (discard)rect;
    (discard)text;
    (discard)brush;
    return true;
}

// screenshot: copy the framebuffer into an Image (RGBA8, 0xRRGGBBAA Bytes)
static bool raster_capture(Image *dest) {
    if (!dest || !s_px) return false;
    if (!Image_ensureShadow(dest, (u32)s_w, (u32)s_h)) return false;
    u8 *out = Image_pixels(dest);
    if (!out) return false;
    bytes outStride = Image_stride(dest);
    for (i32 y = 0; y < s_h; y++) {
        const u32 *row = s_px + (bytes)y * (bytes)s_capW;
        u8 *orow = out + (bytes)y * outStride;
        for (i32 x = 0; x < s_w; x++) {
            u32 c = row[x];
            orow[x * 4 + 0] = (u8)((c >> 24) & 0xFFu);
            orow[x * 4 + 1] = (u8)((c >> 16) & 0xFFu);
            orow[x * 4 + 2] = (u8)((c >> 8) & 0xFFu);
            orow[x * 4 + 3] = (u8)(c & 0xFFu);
        }
    }
    return true;
}

const Backend *RasterGraphics_row() {
    static const Backend row = {
        BACKEND_RASTER, raster_begin,   raster_end,     raster_present,
        raster_resize,  raster_clear,   raster_clip,    raster_fillRect,
        raster_drawImage, raster_drawText, raster_capture,
    };
    return &row;
}
