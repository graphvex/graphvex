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

// Allocates an empty CPU-side quad batch with an initially unbounded clip.
VkBatch *VkBatch_0() {
    VkBatch *b = calloc(1, sizeof *b);
    if (b) (*b).clip = (Rect){-1.0e7f, -1.0e7f, 2.0e7f, 2.0e7f};   // unbounded
    return b;
}

// Frees the quad storage and batch object; null is ignored.
fn VkBatch_free(VkBatch *b) {
    if (!b) return;
    free((*b).quads);
    free(b);
}

// Clears queued quads and restores the default unbounded rectangular clip.
fn VkBatch_clear(VkBatch *b) {
    if (b) {
        (*b).count = 0;
        (*b).clip = (Rect){-1.0e7f, -1.0e7f, 2.0e7f, 2.0e7f};
        (*b).clipRadius = 0.0f;
    }
}

// Sets the rectangular clip applied to subsequently added quads.
fn VkBatch_setClip(VkBatch *b, Rect clip) {
    if (b) (*b).clip = clip;
}

// Sets a nonnegative rounded-clip radius for subsequently added quads.
fn VkBatch_setClipRadius(VkBatch *b, f32 radius) {
    if (b) (*b).clipRadius = radius > 0.0f ? radius : 0.0f;
}

// bake the batch clip into a quad's LOCAL space (the shape is untouched)
// Encodes the batch clip relative to a quad while preserving its original geometry.
static fn clip_quad(VkQuad *q, Rect clip, f32 radius) {
    (*q).cx0 = clip.x - (*q).x;
    (*q).cy0 = clip.y - (*q).y;
    (*q).cx1 = clip.x + clip.w - (*q).x;
    (*q).cy1 = clip.y + clip.h - (*q).y;
    (*q).clipRadius = radius > 0.0f ? radius : 0.0f;
}

// Reserves and initializes one quad slot, returning null if backing growth fails.
static VkQuad *batch_push(VkBatch *b) {
    if ((*b).count == (*b).cap) {
        (*b).cap = (*b).cap ? (*b).cap * 2 : 256;
        VkQuad *grown = realloc((*b).quads, (*b).cap * sizeof *grown);
        if (!grown) { (*b).cap = 0; (*b).count = 0; return nullptr; }
        (*b).quads = grown;
    }
    VkQuad *q = &(*b).quads[(*b).count++];
    memset(q, 0, sizeof *q);
    (*q).u0 = 0.0f; (*q).v0 = 0.0f; (*q).u1 = 1.0f; (*q).v1 = 1.0f;
    return q;
}

// Appends a brush-filled rectangle with the current clip metadata.
fn VkBatch_rect(VkBatch *b, Rect dst, const Brush *brush) {
    if (!b || !brush || Rect_isEmpty(dst)) return;
    VkQuad *q = batch_push(b);
    if (!q) return;
    (*q).x = dst.x; (*q).y = dst.y; (*q).w = dst.w; (*q).h = dst.h;
    (*q).fill = (*brush).color;
    (*q).border = (*brush).border;
    (*q).radius = (*brush).radius;
    (*q).stroke = (*brush).borderWidth;
    (*q).blur = (*brush).blur;
    (*q).mode = 0.0f;
    (*q).texture = 0u;
    clip_quad(q, (*b).clip, (*b).clipRadius);
}

// Appends an image quad whose source rectangle is normalized by image dimensions.
fn VkBatch_image(VkBatch *b, const Image *image, Rect src, Rect dst) {
    if (!b || !image || Rect_isEmpty(dst)) return;
    VkQuad *q = batch_push(b);
    if (!q) return;
    (*q).x = dst.x; (*q).y = dst.y; (*q).w = dst.w; (*q).h = dst.h;
    f32 iw = Image_width(image) > 0 ? (f32)Image_width(image) : 1.0f;
    f32 ih = Image_height(image) > 0 ? (f32)Image_height(image) : 1.0f;
    (*q).u0 = src.x / iw; (*q).v0 = src.y / ih;
    (*q).u1 = (src.x + src.w) / iw; (*q).v1 = (src.y + src.h) / ih;
    (*q).fill = COLOR_WHITE;
    (*q).mode = 1.0f;
    (*q).texture = Image_layer(image);
    clip_quad(q, (*b).clip, (*b).clipRadius);
}

// Appends a tinted glyph quad referencing the supplied atlas layer.
fn VkBatch_glyph(VkBatch *b, Rect dst, u32 atlasLayer, Color color) {
    if (!b || Rect_isEmpty(dst)) return;
    VkQuad *q = batch_push(b);
    if (!q) return;
    (*q).x = dst.x; (*q).y = dst.y; (*q).w = dst.w; (*q).h = dst.h;
    (*q).fill = color;
    (*q).mode = 2.0f;
    (*q).texture = atlasLayer;
    clip_quad(q, (*b).clip, (*b).clipRadius);
}

// Copies quad attributes and the supplied position/UV into one packed vertex.
static fn put(VkVertex *v, const VkQuad *q, f32 x, f32 y, f32 u, f32 vv) {
    (*v).x = x; (*v).y = y;
    (*v).u = u; (*v).v = vv;
    (*v).r = (f32)Color_red((*q).fill) / 255.0f;
    (*v).g = (f32)Color_green((*q).fill) / 255.0f;
    (*v).b = (f32)Color_blue((*q).fill) / 255.0f;
    (*v).a = (f32)Color_alpha((*q).fill) / 255.0f;
    (*v).br = (f32)Color_red((*q).border) / 255.0f;
    (*v).bg = (f32)Color_green((*q).border) / 255.0f;
    (*v).bb = (f32)Color_blue((*q).border) / 255.0f;
    (*v).ba = (f32)Color_alpha((*q).border) / 255.0f;
    (*v).radius = (*q).radius;
    (*v).stroke = (*q).stroke;
    (*v).mode = (*q).mode;
    (*v).layer = (f32)((*q).texture);
    (*v).qw = (*q).w;
    (*v).qh = (*q).h;
    (*v).c0 = (*q).cx0;
    (*v).c1 = (*q).cy0;
    (*v).c2 = (*q).cx1;
    (*v).c3 = (*q).cy1;
    (*v).clipRadius = (*q).clipRadius;
    (*v).blur = (*q).blur;
}

// Returns required vertex count, or expands all quads into six vertices each when capacity suffices.
bytes VkBatch_vertices(const VkBatch *b, VkVertex *out, bytes capacity) {
    if (!b) return 0;
    bytes needed = (*b).count * 6u;
    if (!out || capacity < needed) return needed;
    bytes w = 0;
    for (bytes i = 0; i < (*b).count; i++) {
        const VkQuad *q = &(*b).quads[i];
        f32 x0 = (*q).x, y0 = (*q).y, x1 = (*q).x + (*q).w, y1 = (*q).y + (*q).h;
        // two triangles: (0,0)(1,0)(0,1)  (0,1)(1,0)(1,1)
        put(&out[w++], q, x0, y0, (*q).u0, (*q).v0);
        put(&out[w++], q, x1, y0, (*q).u1, (*q).v0);
        put(&out[w++], q, x0, y1, (*q).u0, (*q).v1);
        put(&out[w++], q, x0, y1, (*q).u0, (*q).v1);
        put(&out[w++], q, x1, y0, (*q).u1, (*q).v0);
        put(&out[w++], q, x1, y1, (*q).u1, (*q).v1);
    }
    return w;
}
