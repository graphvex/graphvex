#include "vex.h"
#include "vulkan/vk_batch.h"

#include <stdlib.h>
#include <string.h>

;;DEFINITION
/** VkBatch is the CPU-side bridge from Graphvex draw requests to a packed
 * vertex stream. It owns growable quad storage, preserves source geometry, and
 * records clipping as local metadata; it owns no Vulkan handle and is directly
 * testable without a device. */
;;OVERVIEW
/** ========================================================================
 * CLASS: VkBatch (vulkan/vk_batch.c)
 * STRUCT FIELDS (Mirroring VkBatch)
 * VkQuad *quads                 owned growable quad array
 * bytes count                  initialized quad count
 * bytes cap                    allocated quad capacity
 * Rect clip                     current native-pixel rectangular clip
 * f32 clipRadius              current rounded clip radius
 * FUNCTION REGISTRY
 * Constructors: VkBatch *VkBatch_0()
 * Lifecycle: fn VkBatch_free(VkBatch *b.sh), fn VkBatch_clear(VkBatch *b.sh)
 * Setters: fn VkBatch_setClip(VkBatch *b.sh, Rect clip)
 *   fn VkBatch_setClipRadius(VkBatch *b.sh, f32 radius)
 * Operations: fn VkBatch_rect(VkBatch *b.sh, Rect dst, const Brush *brush)
 *   fn VkBatch_image(VkBatch *b.sh, const Image *image, Rect src, Rect dst)
 *   fn VkBatch_glyph(VkBatch *b.sh, Rect dst, u32 atlasLayer, Color color)
 * Projection: bytes VkBatch_vertices(const VkBatch *b.sh, VkVertex *out,
 *   bytes capacity)
 * Private helpers: clip_quad converts batch clip to local quad bounds;
 *   batch_push reserves and initializes a quad; put converts quad attributes
 *   into one packed vertex.
 * SLOT RECORD: VkQuad (public behaviorless row used by the batch)
 * f32 x,y,w,h                 native-pixel destination
 * f32 u0,v0,u1,v1             source texture coordinates
 * Color fill, border            packed 0xRRGGBBAA colors
 * f32 radius,stroke,blur,mode shape and sampling parameters
 * u32 texture              atlas/layer selection
 * f32 cx0,cy0,cx1,cy1         local clip rectangle
 * f32 clipRadius              local rounded-clip radius
 * Admission, failure and ownership contract:
 * The batch owns quads and free releases them. Invalid/null inputs are ignored.
 * Vertex projection returns required count without writing when output is null
 * or too small. A failed growth currently clears the batch count/capacity; this
 * failure disposition is part of the current implementation, not rollback.
 * ======================================================================== */

;;GETTER
static inline VkQuad *VkBatch_quads(const VkBatch *self) { return (*self).quads; }
;;SETTER
static inline fn VkBatch_setQuads(VkBatch *self, VkQuad *v) { (*self).quads = v; }
;;GETTER
static inline bytes VkBatch_count(const VkBatch *self) { return (*self).count; }
;;SETTER
static inline fn VkBatch_setCount(VkBatch *self, bytes v) { (*self).count = v; }
;;GETTER
static inline bytes VkBatch_cap(const VkBatch *self) { return (*self).cap; }
;;SETTER
static inline fn VkBatch_setCap(VkBatch *self, bytes v) { (*self).cap = v; }
;;GETTER
static inline Rect VkBatch_clip(const VkBatch *self) { return (*self).clip; }
;;GETTER
static inline f32 VkBatch_clipRadius(const VkBatch *self) { return (*self).clipRadius; }

// Allocates an empty CPU-side quad batch with an initially unbounded clip.
VkBatch *VkBatch_0() {
    VkBatch *b = calloc(1, sizeof *b);
    if (b) VkBatch_setClip(b, (Rect){-1.0e7f, -1.0e7f, 2.0e7f, 2.0e7f});   // unbounded
    return b;
}

// Frees the quad storage and batch object; null is ignored.
fn VkBatch_free(VkBatch *b) {
    if (!b) return;
    free(VkBatch_quads(b));
    free(b);
}

// Clears queued quads and restores the default unbounded rectangular clip.
fn VkBatch_clear(VkBatch *b) {
    if (b) {
        VkBatch_setCount(b, 0);
        VkBatch_setClip(b, (Rect){-1.0e7f, -1.0e7f, 2.0e7f, 2.0e7f});
        VkBatch_setClipRadius(b, 0.0f);
    }
}

// Sets the rectangular clip applied to subsequently added quads.
;;SETTER
fn VkBatch_setClip(VkBatch *b, Rect clip) {
    if (b) (*b).clip = clip;
}

// Sets a nonnegative rounded-clip radius for subsequently added quads.
;;SETTER
fn VkBatch_setClipRadius(VkBatch *b, f32 radius) {
    if (b) (*b).clipRadius = radius > 0.0f ? radius : 0.0f;
}

// bake the batch clip into a quad's LOCAL space (the shape is untouched)
// Encodes the batch clip relative to a quad while preserving its original geometry.
static VkQuad clip_quad(VkQuad q, Rect clip, f32 radius) {
    q.cx0 = clip.x - q.x;
    q.cy0 = clip.y - q.y;
    q.cx1 = clip.x + clip.w - q.x;
    q.cy1 = clip.y + clip.h - q.y;
    q.clipRadius = radius > 0.0f ? radius : 0.0f;
    return q;
}

// Reserves and initializes one quad slot, returning null if backing growth fails.
static VkQuad *batch_push(VkBatch *b) {
    if (VkBatch_count(b) == VkBatch_cap(b)) {
        bytes nextCap = VkBatch_cap(b) ? VkBatch_cap(b) * 2 : 256;
        VkBatch_setCap(b, nextCap);
        VkQuad *grown = realloc(VkBatch_quads(b), nextCap * sizeof *grown);
        if (!grown) { VkBatch_setCap(b, 0); VkBatch_setCount(b, 0); return nullptr; }
        VkBatch_setQuads(b, grown);
    }
    bytes idx = VkBatch_count(b);
    VkBatch_setCount(b, idx + 1);
    VkQuad *q = &VkBatch_quads(b)[idx];
    *q = (VkQuad){ .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f };
    return q;
}

// Appends a brush-filled rectangle with the current clip metadata.
fn VkBatch_rect(VkBatch *b, Rect dst, const Brush *brush) {
    if (!b || !brush || Rect_isEmpty(dst)) return;
    VkQuad *slot = batch_push(b);
    if (!slot) return;
    Brush br = *brush;
    VkQuad q = {
        .x = dst.x, .y = dst.y, .w = dst.w, .h = dst.h,
        .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
        .fill = br.color,
        .border = br.border,
        .radius = br.radius,
        .stroke = br.borderWidth,
        .blur = br.blur,
        .mode = 0.0f,
        .texture = 0u,
    };
    *slot = clip_quad(q, VkBatch_clip(b), VkBatch_clipRadius(b));
}

// Appends an image quad whose source rectangle is normalized by image dimensions.
fn VkBatch_image(VkBatch *b, const Image *image, Rect src, Rect dst) {
    if (!b || !image || Rect_isEmpty(dst)) return;
    VkQuad *slot = batch_push(b);
    if (!slot) return;
    f32 iw = Image_width(image) > 0 ? (f32)Image_width(image) : 1.0f;
    f32 ih = Image_height(image) > 0 ? (f32)Image_height(image) : 1.0f;
    VkQuad q = {
        .x = dst.x, .y = dst.y, .w = dst.w, .h = dst.h,
        .u0 = src.x / iw, .v0 = src.y / ih,
        .u1 = (src.x + src.w) / iw, .v1 = (src.y + src.h) / ih,
        .fill = COLOR_WHITE,
        .mode = 1.0f,
        .texture = Image_layer(image),
    };
    *slot = clip_quad(q, VkBatch_clip(b), VkBatch_clipRadius(b));
}

// Appends a tinted glyph quad referencing the supplied atlas layer.
fn VkBatch_glyph(VkBatch *b, Rect dst, u32 atlasLayer, Color color) {
    if (!b || Rect_isEmpty(dst)) return;
    VkQuad *slot = batch_push(b);
    if (!slot) return;
    VkQuad q = {
        .x = dst.x, .y = dst.y, .w = dst.w, .h = dst.h,
        .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
        .fill = color,
        .mode = 2.0f,
        .texture = atlasLayer,
    };
    *slot = clip_quad(q, VkBatch_clip(b), VkBatch_clipRadius(b));
}

// Copies quad attributes and the supplied position/UV into one packed vertex.
static fn put(VkVertex *v, VkQuad q, f32 x, f32 y, f32 u, f32 vv) {
    *v = (VkVertex){
        .x = x, .y = y,
        .u = u, .v = vv,
        .r = (f32)Color_red(q.fill) / 255.0f,
        .g = (f32)Color_green(q.fill) / 255.0f,
        .b = (f32)Color_blue(q.fill) / 255.0f,
        .a = (f32)Color_alpha(q.fill) / 255.0f,
        .br = (f32)Color_red(q.border) / 255.0f,
        .bg = (f32)Color_green(q.border) / 255.0f,
        .bb = (f32)Color_blue(q.border) / 255.0f,
        .ba = (f32)Color_alpha(q.border) / 255.0f,
        .radius = q.radius,
        .stroke = q.stroke,
        .mode = q.mode,
        .layer = (f32)q.texture,
        .qw = q.w,
        .qh = q.h,
        .c0 = q.cx0,
        .c1 = q.cy0,
        .c2 = q.cx1,
        .c3 = q.cy1,
        .clipRadius = q.clipRadius,
        .blur = q.blur,
    };
}

// Returns required vertex count, or expands all quads into six vertices each when capacity suffices.
bytes VkBatch_vertices(const VkBatch *b, VkVertex *out, bytes capacity) {
    if (!b) return 0;
    bytes needed = VkBatch_count(b) * 6u;
    if (!out || capacity < needed) return needed;
    bytes w = 0;
    for (bytes i = 0; i < VkBatch_count(b); i++) {
        VkQuad q = VkBatch_quads(b)[i];
        f32 x0 = q.x, y0 = q.y, x1 = q.x + q.w, y1 = q.y + q.h;
        // two triangles: (0,0)(1,0)(0,1)  (0,1)(1,0)(1,1)
        put(&out[w++], q, x0, y0, q.u0, q.v0);
        put(&out[w++], q, x1, y0, q.u1, q.v0);
        put(&out[w++], q, x0, y1, q.u0, q.v1);
        put(&out[w++], q, x0, y1, q.u0, q.v1);
        put(&out[w++], q, x1, y0, q.u1, q.v0);
        put(&out[w++], q, x1, y1, q.u1, q.v1);
    }
    return w;
}
