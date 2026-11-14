#ifndef GRAPHICS_VK_BATCH_H
#define GRAPHICS_VK_BATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/constructor.h"

#include "graphics/graphics.h"
#include "graphvex/lang/image.h"

// graphvex R3 — vulkan/vk_batch.h
//
// THE QUAD BATCHER. The whole renderer is "turn the display list into one
// vertex buffer of textured quads and issue a handful of draw calls." This is
// pure CPU data — no Vulkan handle — so it is unit-testable headless and the
// GPU side only ever uploads + draws it.
//
// One vertex per corner. A quad is two triangles (6 vertices). Every rectangle,
// image and glyph is a quad; the fragment shader does the rounded-corner SDF,
// the border, and the (future) glyph/alpha sampling.

typedef struct VkQuad {
    // destination, native px
    f32 x, y, w, h;
    // source UVs in [0,1] (image); rects use 0..1 solid
    f32 u0, v0, u1, v1;
    Color fill;
    Color border;
    f32 radius;    // corner radius, px
    f32 stroke;    // border width, px (0 = none)
    f32 blur;      // soft-edge falloff, px (0 = hard)
    f32 mode;      // -1 = pre-sampled pixels (no shape AA), 0 solid, 1 image, 2 glyph
    u32 texture; // atlas/layer id (0 = white)
    // clip window in the quad's LOCAL space (0..w, 0..h). The shape keeps its
    // true size; fragments outside this window are discarded, never resized.
    f32 cx0, cy0, cx1, cy1;
    f32 clipRadius;  // clip corner radius (0 = rectangular)
} VkQuad;

typedef struct VkBatch {
    VkQuad *quads;
    bytes count;
    bytes cap;
    Rect clip;       // clip applied to every quad recorded (native px)
    f32 clipRadius; // corner radius of that clip (0 = rectangular)
} VkBatch;

// One interleaved vertex: pos(2) uv(2) fill(4) border(4) params(4) quadSize(2)
//                        + clipLocal(4) + clipRadius(1) + blur(1)
#define VK_VERTEX_FLOATS 24u

typedef struct VkVertex {
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;         // fill
    f32 br, bg, bb, ba;     // border
    f32 radius, stroke, mode, layer;
    f32 qw, qh;             // quad size in px (for the rounded-rect SDF)
    f32 c0, c1, c2, c3;     // local-space clip bounds (x0,y0,x1,y1)
    f32 clipRadius;         // clip corner radius (0 = rectangular)
    f32 blur;               // soft-edge falloff, px
} VkVertex;

VkBatch *VkBatch_0();
#define VkBatch(...) CONSTRUCTOR_DISPATCH(VkBatch, __VA_ARGS__)
fn VkBatch_free(VkBatch *b);
fn VkBatch_clear(VkBatch *b);

// Set the clip recorded into every quad from here on (native px). Quads keep
// their geometry; the clip only cuts fragments.
fn VkBatch_setClip(VkBatch *b, Rect clip);
fn VkBatch_setClipRadius(VkBatch *b, f32 radius);   // 0 = rectangular clip

fn VkBatch_rect(VkBatch *b, Rect dst, const Brush *brush);
fn VkBatch_image(VkBatch *b, const Image *image, Rect src, Rect dst);
fn VkBatch_glyph(VkBatch *b, Rect dst, u32 atlasLayer, Color color);

// Expand to 6 vertices per quad. Returns the number of vertices written; if
// `out` is null or `capacity` is too small, returns the required count and
// writes nothing (caller sizes the upload).
bytes VkBatch_vertices(const VkBatch *b, VkVertex *out, bytes capacity);

#endif // GRAPHICS_VK_BATCH_H
