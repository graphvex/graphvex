#include <metal_stdlib>
using namespace metal;

struct GlyphPrim {
    float x;
    float y;
    float width;
    float height;
    float uvX;
    float uvY;
    float uvW;
    float uvH;
    float clipX0;
    float clipY0;
    float clipX1;
    float clipY1;
    uint colorRGBA;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct TextPassUniform {
    uint dstWidth;
    uint dstHeight;
    uint dstStrideBytes;
    uint glyphCount;
    uint atlasWidth;
    uint atlasHeight;
    uint atlasStrideBytes;
    uint _pad0;
};

static inline float textSrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : pow((c + 0.055f) / 1.055f, 2.4f);
}

static inline float4 textUnpackLinearPremul(uint rgba, float coverage) {
    float a = (float(rgba & 0xFFu) / 255.0f) * coverage;
    if (a <= 0.0f) return float4(0.0f);
    float r = textSrgbToLinear(float((rgba >> 24) & 0xFFu) / 255.0f) * a;
    float g = textSrgbToLinear(float((rgba >> 16) & 0xFFu) / 255.0f) * a;
    float b = textSrgbToLinear(float((rgba >> 8) & 0xFFu) / 255.0f) * a;
    return float4(r, g, b, a);
}

static inline float textSampleAtlasAlpha(device const uchar *atlas,
                                         constant TextPassUniform &u,
                                         float2 uv) {
    if (u.atlasWidth == 0u || u.atlasHeight == 0u) return 0.0f;
    float fx = clamp(uv.x - 0.5f, 0.0f, float(u.atlasWidth - 1u));
    float fy = clamp(uv.y - 0.5f, 0.0f, float(u.atlasHeight - 1u));
    uint x0 = uint(floor(fx));
    uint y0 = uint(floor(fy));
    uint x1 = min(x0 + 1u, u.atlasWidth - 1u);
    uint y1 = min(y0 + 1u, u.atlasHeight - 1u);
    float tx = fx - float(x0);
    float ty = fy - float(y0);

    ulong s = ulong(u.atlasStrideBytes);
    float a00 = float(atlas[ulong(y0) * s + ulong(x0)]) / 255.0f;
    float a10 = float(atlas[ulong(y0) * s + ulong(x1)]) / 255.0f;
    float a01 = float(atlas[ulong(y1) * s + ulong(x0)]) / 255.0f;
    float a11 = float(atlas[ulong(y1) * s + ulong(x1)]) / 255.0f;
    float top = mix(a00, a10, tx);
    float bot = mix(a01, a11, tx);
    return mix(top, bot, ty);
}

struct TextVertOut {
    float4 position [[position]];
    float2 pixelPos;
    float2 atlasUV;
    uint instanceId [[flat]];
};

vertex TextVertOut collage_text_vert(uint vid [[vertex_id]],
                                     uint iid [[instance_id]],
                                     device const GlyphPrim *prims [[buffer(0)]],
                                     constant TextPassUniform &u [[buffer(1)]]) {
    TextVertOut out;
    out.instanceId = iid;
    GlyphPrim g = prims[iid];
    if (g.width <= 0.0f || g.height <= 0.0f || u.dstWidth == 0u || u.dstHeight == 0u) {
        out.position = float4(-2.0f, -2.0f, 0.0f, 1.0f);
        out.pixelPos = float2(0.0f);
        out.atlasUV = float2(0.0f);
        return out;
    }

    float sx = (vid & 1u) ? 1.0f : 0.0f;
    float sy = (vid & 2u) ? 1.0f : 0.0f;
    float2 corner = float2(g.x + sx * g.width, g.y + sy * g.height);
    float ndcX = (corner.x / float(u.dstWidth)) * 2.0f - 1.0f;
    float ndcY = 1.0f - (corner.y / float(u.dstHeight)) * 2.0f;

    out.position = float4(ndcX, ndcY, 0.0f, 1.0f);
    out.pixelPos = corner;
    out.atlasUV = float2(g.uvX + sx * g.uvW, g.uvY + sy * g.uvH);
    return out;
}

fragment float4 collage_text_frag(TextVertOut in [[stage_in]],
                                  device const GlyphPrim *prims [[buffer(0)]],
                                  constant TextPassUniform &u [[buffer(1)]],
                                  device const uchar *atlas [[buffer(2)]]) {
    GlyphPrim g = prims[in.instanceId];
    float px = in.pixelPos.x;
    float py = in.pixelPos.y;

    if (g.clipX1 > g.clipX0 && g.clipY1 > g.clipY0) {
        if (px < g.clipX0 || px > g.clipX1 || py < g.clipY0 || py > g.clipY1) {
            discard_fragment();
        }
    }

    float cov = textSampleAtlasAlpha(atlas, u, in.atlasUV);
    if (cov <= 1e-4f) discard_fragment();
    return textUnpackLinearPremul(g.colorRGBA, cov);
}
