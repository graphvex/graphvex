#include <metal_stdlib>
using namespace metal;

struct RectPrim {
    float x;
    float y;
    float width;
    float height;
    float radiusTL;
    float radiusTR;
    float radiusBR;
    float radiusBL;
    float clipX0;
    float clipY0;
    float clipX1;
    float clipY1;
    uint fillRGBA;
    uint borderRGBA;
    float borderWidth;
    float shadowBlur;
    uint flags;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    uint _pad3;
    uint _pad4;
    uint _pad5;
    uint _pad6;
};

struct RectPassUniform {
    uint dstWidth;
    uint dstHeight;
    uint dstStrideBytes;
    uint rectCount;
};

constant uint RECT_PRIM_SHADOW = 1u;

static inline float rectSrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : pow((c + 0.055f) / 1.055f, 2.4f);
}

static inline float rectLinearToSrgb(float c) {
    c = clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
}

static inline float4 rectUnpackLinearPremul(uint rgba, float coverage) {
    float a = (float(rgba & 0xFFu) / 255.0f) * coverage;
    if (a <= 0.0f) return float4(0.0f);
    float r = rectSrgbToLinear(float((rgba >> 24) & 0xFFu) / 255.0f) * a;
    float g = rectSrgbToLinear(float((rgba >> 16) & 0xFFu) / 255.0f) * a;
    float b = rectSrgbToLinear(float((rgba >> 8) & 0xFFu) / 255.0f) * a;
    return float4(r, g, b, a);
}

static inline float4 rectReadDstLinear(device const uchar *buf, uint strideBytes, uint x, uint y) {
    ulong off = ulong(y) * ulong(strideBytes) + ulong(x) * 4ul;
    float a = float(buf[off + 3]) / 255.0f;
    float r = rectSrgbToLinear(float(buf[off + 0]) / 255.0f) * a;
    float g = rectSrgbToLinear(float(buf[off + 1]) / 255.0f) * a;
    float b = rectSrgbToLinear(float(buf[off + 2]) / 255.0f) * a;
    return float4(r, g, b, a);
}

static inline void rectWriteDstLinear(device uchar *buf, uint strideBytes, uint x, uint y, float4 p) {
    ulong off = ulong(y) * ulong(strideBytes) + ulong(x) * 4ul;
    float a = clamp(p.a, 0.0f, 1.0f);
    float invA = a > 1e-6f ? 1.0f / a : 0.0f;
    buf[off + 0] = uchar(clamp(rectLinearToSrgb(p.r * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    buf[off + 1] = uchar(clamp(rectLinearToSrgb(p.g * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    buf[off + 2] = uchar(clamp(rectLinearToSrgb(p.b * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    buf[off + 3] = uchar(clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
}

// Signed distance to a rounded box centered at origin with half-size b and
// per-corner radii (tl, tr, br, bl). Negative inside, positive outside.
static inline float rectSdRoundedBox(float2 p, float2 b, float tl, float tr, float br, float bl) {
    float r = (p.x > 0.0f) ? ((p.y > 0.0f) ? br : tr) : ((p.y > 0.0f) ? bl : tl);
    float maxR = min(b.x, b.y);
    r = clamp(r, 0.0f, maxR);
    float2 q = abs(p) - b + float2(r);
    return min(max(q.x, q.y), 0.0f) + length(max(q, float2(0.0f))) - r;
}

kernel void collage_rect_pass_rgba8(device uchar *dst [[buffer(0)]],
                                    device const RectPrim *prims [[buffer(1)]],
                                    constant RectPassUniform &u [[buffer(2)]],
                                    uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.dstWidth || gid.y >= u.dstHeight) return;

    float px = float(gid.x) + 0.5f;
    float py = float(gid.y) + 0.5f;
    float4 acc = rectReadDstLinear(dst, u.dstStrideBytes, gid.x, gid.y);

    for (uint i = 0u; i < u.rectCount; i++) {
        RectPrim r = prims[i];
        if (r.width <= 0.0f || r.height <= 0.0f) continue;

        if (r.clipX1 > r.clipX0 && r.clipY1 > r.clipY0) {
            if (px < r.clipX0 || px > r.clipX1 || py < r.clipY0 || py > r.clipY1) continue;
        }

        float pad = ((r.flags & RECT_PRIM_SHADOW) != 0u) ? max(r.shadowBlur * 2.5f, 2.0f) : 1.5f;
        if (px < r.x - pad || px > r.x + r.width + pad ||
            py < r.y - pad || py > r.y + r.height + pad) {
            continue;
        }

        float2 halfSize = float2(r.width * 0.5f, r.height * 0.5f);
        float2 center = float2(r.x + halfSize.x, r.y + halfSize.y);
        float dist = rectSdRoundedBox(float2(px, py) - center, halfSize,
                                      r.radiusTL, r.radiusTR, r.radiusBR, r.radiusBL);

        if ((r.flags & RECT_PRIM_SHADOW) != 0u) {
            float sigma = max(r.shadowBlur * 0.5f, 0.5f);
            float cov = (dist <= 0.0f)
                ? 1.0f
                : exp(-(dist * dist) / (2.0f * sigma * sigma));
            if (cov > 1e-4f) {
                float4 src = rectUnpackLinearPremul(r.fillRGBA, cov);
                acc = src + acc * (1.0f - src.a);
            }
            continue;
        }

        float outerCov = clamp(0.5f - dist, 0.0f, 1.0f);
        if (outerCov <= 0.0f) continue;

        if (r.borderWidth > 0.0f && (r.borderRGBA & 0xFFu) != 0u) {
            float innerCov = clamp(0.5f - (dist + r.borderWidth), 0.0f, 1.0f);
            float borderCov = clamp(outerCov - innerCov, 0.0f, 1.0f);
            if ((r.fillRGBA & 0xFFu) != 0u) {
                float4 fillSrc = rectUnpackLinearPremul(r.fillRGBA, outerCov);
                acc = fillSrc + acc * (1.0f - fillSrc.a);
            }
            if (borderCov > 0.0f) {
                float4 borderSrc = rectUnpackLinearPremul(r.borderRGBA, borderCov);
                acc = borderSrc + acc * (1.0f - borderSrc.a);
            }
        } else if ((r.fillRGBA & 0xFFu) != 0u) {
            float4 fillSrc = rectUnpackLinearPremul(r.fillRGBA, outerCov);
            acc = fillSrc + acc * (1.0f - fillSrc.a);
        }
    }

    rectWriteDstLinear(dst, u.dstStrideBytes, gid.x, gid.y, acc);
}

// ---------------------------------------------------------------- Hardware-instanced quad pass

struct RectVertOut {
    float4 position [[position]];
    float2 pixelPos;
    uint instanceId [[flat]];
};

vertex RectVertOut collage_rect_vert(uint vid [[vertex_id]],
                                     uint iid [[instance_id]],
                                     device const RectPrim *prims [[buffer(0)]],
                                     constant RectPassUniform &u [[buffer(1)]]) {
    RectVertOut out;
    out.instanceId = iid;
    RectPrim r = prims[iid];
    if (r.width <= 0.0f || r.height <= 0.0f || u.dstWidth == 0u || u.dstHeight == 0u) {
        out.position = float4(-2.0f, -2.0f, 0.0f, 1.0f);
        out.pixelPos = float2(0.0f);
        return out;
    }

    float pad = ((r.flags & RECT_PRIM_SHADOW) != 0u) ? max(r.shadowBlur * 2.5f, 2.0f) : 1.5f;
    float x0 = r.x - pad;
    float y0 = r.y - pad;
    float x1 = r.x + r.width + pad;
    float y1 = r.y + r.height + pad;

    float2 corner = float2((vid & 1u) ? x1 : x0, (vid & 2u) ? y1 : y0);
    float ndcX = (corner.x / float(u.dstWidth)) * 2.0f - 1.0f;
    float ndcY = 1.0f - (corner.y / float(u.dstHeight)) * 2.0f;
    out.position = float4(ndcX, ndcY, 0.0f, 1.0f);
    out.pixelPos = corner;
    return out;
}

fragment float4 collage_rect_frag(RectVertOut in [[stage_in]],
                                  device const RectPrim *prims [[buffer(0)]]) {
    RectPrim r = prims[in.instanceId];
    float px = in.pixelPos.x;
    float py = in.pixelPos.y;

    if (r.clipX1 > r.clipX0 && r.clipY1 > r.clipY0) {
        if (px < r.clipX0 || px > r.clipX1 || py < r.clipY0 || py > r.clipY1) {
            discard_fragment();
        }
    }

    float2 halfSize = float2(r.width * 0.5f, r.height * 0.5f);
    float2 center = float2(r.x + halfSize.x, r.y + halfSize.y);
    float dist = rectSdRoundedBox(float2(px, py) - center, halfSize,
                                  r.radiusTL, r.radiusTR, r.radiusBR, r.radiusBL);

    if ((r.flags & RECT_PRIM_SHADOW) != 0u) {
        float sigma = max(r.shadowBlur * 0.5f, 0.5f);
        float cov = (dist <= 0.0f)
            ? 1.0f
            : exp(-(dist * dist) / (2.0f * sigma * sigma));
        if (cov <= 1e-4f) discard_fragment();
        return rectUnpackLinearPremul(r.fillRGBA, cov);
    }

    float outerCov = clamp(0.5f - dist, 0.0f, 1.0f);
    if (outerCov <= 0.0f) discard_fragment();

    float4 outColor = float4(0.0f);
    if (r.borderWidth > 0.0f && (r.borderRGBA & 0xFFu) != 0u) {
        float innerCov = clamp(0.5f - (dist + r.borderWidth), 0.0f, 1.0f);
        float borderCov = clamp(outerCov - innerCov, 0.0f, 1.0f);
        if ((r.fillRGBA & 0xFFu) != 0u) {
            outColor = rectUnpackLinearPremul(r.fillRGBA, outerCov);
        }
        if (borderCov > 0.0f) {
            float4 borderSrc = rectUnpackLinearPremul(r.borderRGBA, borderCov);
            outColor = borderSrc + outColor * (1.0f - borderSrc.a);
        }
    } else if ((r.fillRGBA & 0xFFu) != 0u) {
        outColor = rectUnpackLinearPremul(r.fillRGBA, outerCov);
    }
    if (outColor.a <= 1e-5f) discard_fragment();
    return outColor;
}
