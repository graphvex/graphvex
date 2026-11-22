#include <metal_stdlib>
using namespace metal;

struct BlurUniform {
    uint srcWidth;
    uint srcHeight;
    uint srcStrideBytes;
    uint dstWidth;
    uint dstHeight;
    uint dstStrideBytes;
    int offsetX;
    int offsetY;
    int radius;
};

struct BlendUniform {
    uint dstWidth;
    uint dstHeight;
    uint dstStrideBytes;
    uint srcWidth;
    uint srcHeight;
    uint srcStrideBytes;
    int dstX;
    int dstY;
    int clipX0;
    int clipY0;
    int clipX1;
    int clipY1;
};

static inline float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : pow((c + 0.055f) / 1.055f, 2.4f);
}

static inline float linearToSrgb(float c) {
    c = clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
}

static inline float4 readLinearPremul(device const uchar *buf, uint strideBytes, int x, int y, uint w, uint h) {
    if (x < 0 || y < 0 || uint(x) >= w || uint(y) >= h) {
        return float4(0.0f);
    }
    ulong off = ulong(y) * ulong(strideBytes) + ulong(x) * 4ul;
    float a = float(buf[off + 3]) / 255.0f;
    float r = srgbToLinear(float(buf[off + 0]) / 255.0f) * a;
    float g = srgbToLinear(float(buf[off + 1]) / 255.0f) * a;
    float b = srgbToLinear(float(buf[off + 2]) / 255.0f) * a;
    return float4(r, g, b, a);
}

static inline void writeLinearPremul(device uchar *buf, uint strideBytes, uint x, uint y, float4 p) {
    ulong off = ulong(y) * ulong(strideBytes) + ulong(x) * 4ul;
    float a = clamp(p.a, 0.0f, 1.0f);
    float invA = a > 1e-6f ? 1.0f / a : 0.0f;
    buf[off + 0] = uchar(clamp(linearToSrgb(p.r * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    buf[off + 1] = uchar(clamp(linearToSrgb(p.g * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    buf[off + 2] = uchar(clamp(linearToSrgb(p.b * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    buf[off + 3] = uchar(clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
}

kernel void collage_blur_h_rgba8(device uchar *dst [[buffer(0)]],
                                 device const uchar *src [[buffer(1)]],
                                 constant BlurUniform &u [[buffer(2)]],
                                 uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.dstWidth || gid.y >= u.dstHeight) return;
    int r = max(u.radius, 0);
    float invD = 1.0f / float(2 * r + 1);
    float4 acc = float4(0.0f);
    int sy = int(gid.y) + u.offsetY;
    for (int dx = -r; dx <= r; dx++) {
        int sx = int(gid.x) + u.offsetX + dx;
        acc += readLinearPremul(src, u.srcStrideBytes, sx, sy, u.srcWidth, u.srcHeight);
    }
    writeLinearPremul(dst, u.dstStrideBytes, gid.x, gid.y, acc * invD);
}

kernel void collage_blur_v_rgba8(device uchar *dst [[buffer(0)]],
                                 device const uchar *src [[buffer(1)]],
                                 constant BlurUniform &u [[buffer(2)]],
                                 uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.dstWidth || gid.y >= u.dstHeight) return;
    int r = max(u.radius, 0);
    float invD = 1.0f / float(2 * r + 1);
    float4 acc = float4(0.0f);
    int sx = int(gid.x) + u.offsetX;
    for (int dy = -r; dy <= r; dy++) {
        int sy = int(gid.y) + u.offsetY + dy;
        acc += readLinearPremul(src, u.srcStrideBytes, sx, sy, u.srcWidth, u.srcHeight);
    }
    writeLinearPremul(dst, u.dstStrideBytes, gid.x, gid.y, acc * invD);
}

kernel void collage_blend_over_rgba8(device uchar *dst [[buffer(0)]],
                                     device const uchar *src [[buffer(1)]],
                                     constant BlendUniform &u [[buffer(2)]],
                                     uint2 gid [[thread_position_in_grid]]) {
    int x = u.dstX + int(gid.x);
    int y = u.dstY + int(gid.y);
    if (gid.x >= u.srcWidth || gid.y >= u.srcHeight) return;
    if (x < 0 || y < 0 || uint(x) >= u.dstWidth || uint(y) >= u.dstHeight) return;
    if (u.clipX1 > u.clipX0 && u.clipY1 > u.clipY0) {
        if (x < u.clipX0 || x >= u.clipX1 || y < u.clipY0 || y >= u.clipY1) return;
    }
    float4 s = readLinearPremul(src, u.srcStrideBytes, int(gid.x), int(gid.y), u.srcWidth, u.srcHeight);
    float4 d = readLinearPremul(dst, u.dstStrideBytes, x, y, u.dstWidth, u.dstHeight);
    float4 out = s + d * (1.0f - s.a);
    writeLinearPremul(dst, u.dstStrideBytes, uint(x), uint(y), out);
}
