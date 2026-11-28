#include <metal_stdlib>
using namespace metal;

struct ClearUniform {
    uint strideBytes;
    int x0;
    int y0;
    int x1;
    int y1;
    uint rgba;
};

struct CopyUniform {
    uint dstStrideBytes;
    uint srcStrideBytes;
    uint bpp;
    int dstX0;
    int dstY0;
    int srcX0;
    int srcY0;
    uint copyW;
    uint copyH;
};

struct PresentUniform {
    uint srcWidth;
    uint srcHeight;
    uint srcStrideBytes;
    uint dstWidth;
    uint dstHeight;
};

kernel void graphvex_clear_rgba8(device uchar *dst [[buffer(0)]],
                                 constant ClearUniform &u [[buffer(1)]],
                                 uint2 gid [[thread_position_in_grid]]) {
    int x = u.x0 + int(gid.x);
    int y = u.y0 + int(gid.y);
    if (x >= u.x1 || y >= u.y1) return;
    ulong off = ulong(y) * ulong(u.strideBytes) + ulong(x) * 4ul;
    dst[off + 0] = uchar((u.rgba >> 24) & 0xFFu);
    dst[off + 1] = uchar((u.rgba >> 16) & 0xFFu);
    dst[off + 2] = uchar((u.rgba >> 8)  & 0xFFu);
    dst[off + 3] = uchar(u.rgba & 0xFFu);
}

kernel void graphvex_copy_bytes(device uchar *dst [[buffer(0)]],
                                device const uchar *src [[buffer(1)]],
                                constant CopyUniform &u [[buffer(2)]],
                                uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.copyW || gid.y >= u.copyH) return;
    ulong dOff = ulong(u.dstY0 + int(gid.y)) * ulong(u.dstStrideBytes) +
                 ulong(u.dstX0 + int(gid.x)) * ulong(u.bpp);
    ulong sOff = ulong(u.srcY0 + int(gid.y)) * ulong(u.srcStrideBytes) +
                 ulong(u.srcX0 + int(gid.x)) * ulong(u.bpp);
    for (uint i = 0; i < u.bpp; i++) {
        dst[dOff + i] = src[sOff + i];
    }
}

kernel void graphvex_present_rgba8(device const uchar *src [[buffer(0)]],
                                   texture2d<float, access::write> outTex [[texture(0)]],
                                   constant PresentUniform &u [[buffer(1)]],
                                   uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.dstWidth || gid.y >= u.dstHeight) return;
    if (gid.x >= u.srcWidth || gid.y >= u.srcHeight) {
        outTex.write(float4(0.0, 0.0, 0.0, 1.0), gid);
        return;
    }
    ulong off = ulong(gid.y) * ulong(u.srcStrideBytes) + ulong(gid.x) * 4ul;
    float4 c = float4(float(src[off + 0]),
                      float(src[off + 1]),
                      float(src[off + 2]),
                      float(src[off + 3])) / 255.0;
    outTex.write(c, gid);
}

kernel void graphvex_copy_to_tex_rgba8(device const uchar *src [[buffer(0)]],
                                       texture2d<float, access::write> outTex [[texture(0)]],
                                       constant CopyUniform &u [[buffer(1)]],
                                       uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.copyW || gid.y >= u.copyH) return;
    ulong sOff = ulong(u.srcY0 + int(gid.y)) * ulong(u.srcStrideBytes) +
                 ulong(u.srcX0 + int(gid.x)) * 4ul;
    float4 c = float4(float(src[sOff + 0]),
                      float(src[sOff + 1]),
                      float(src[sOff + 2]),
                      float(src[sOff + 3])) / 255.0;
    outTex.write(c, uint2(uint(u.dstX0) + gid.x, uint(u.dstY0) + gid.y));
}

kernel void graphvex_present_backing_rgba(texture2d<float, access::read> srcTex [[texture(0)]],
                                          texture2d<float, access::write> outTex [[texture(1)]],
                                          constant PresentUniform &u [[buffer(0)]],
                                          uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.dstWidth || gid.y >= u.dstHeight) return;
    if (gid.x >= u.srcWidth || gid.y >= u.srcHeight) {
        outTex.write(float4(0.0, 0.0, 0.0, 1.0), gid);
        return;
    }
    float4 c = srcTex.read(gid);
    outTex.write(c, gid);
}

kernel void graphvex_present_backing_nv12(texture2d<float, access::read> yTex [[texture(0)]],
                                          texture2d<float, access::read> uvTex [[texture(1)]],
                                          texture2d<float, access::write> outTex [[texture(2)]],
                                          constant PresentUniform &u [[buffer(0)]],
                                          uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.dstWidth || gid.y >= u.dstHeight) return;
    if (gid.x >= u.srcWidth || gid.y >= u.srcHeight) {
        outTex.write(float4(0.0, 0.0, 0.0, 1.0), gid);
        return;
    }
    float yv = (yTex.read(gid).r - (16.0 / 255.0)) * (255.0 / 219.0);
    float2 uv = uvTex.read(uint2(gid.x >> 1u, gid.y >> 1u)).rg - float2(128.0 / 255.0);
    float cb = uv.x * (255.0 / 224.0);
    float cr = uv.y * (255.0 / 224.0);
    float3 rgb = clamp(float3(yv + 1.5748 * cr,
                              yv - 0.1873 * cb - 0.4681 * cr,
                              yv + 1.8556 * cb), 0.0, 1.0);
    outTex.write(float4(rgb, 1.0), gid);
}

kernel void graphvex_copy_backing_rgba(device uchar *dst [[buffer(0)]],
                                       texture2d<float, access::read> srcTex [[texture(0)]],
                                       constant CopyUniform &u [[buffer(1)]],
                                       uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.copyW || gid.y >= u.copyH) return;
    float4 c = clamp(srcTex.read(uint2(uint(u.srcX0) + gid.x, uint(u.srcY0) + gid.y)), 0.0, 1.0);
    ulong dOff = ulong(u.dstY0 + int(gid.y)) * ulong(u.dstStrideBytes) +
                 ulong(u.dstX0 + int(gid.x)) * 4ul;
    dst[dOff + 0] = uchar(c.r * 255.0 + 0.5);
    dst[dOff + 1] = uchar(c.g * 255.0 + 0.5);
    dst[dOff + 2] = uchar(c.b * 255.0 + 0.5);
    dst[dOff + 3] = uchar(c.a * 255.0 + 0.5);
}

kernel void graphvex_copy_backing_nv12(device uchar *dst [[buffer(0)]],
                                       texture2d<float, access::read> yTex [[texture(0)]],
                                       texture2d<float, access::read> uvTex [[texture(1)]],
                                       constant CopyUniform &u [[buffer(1)]],
                                       uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.copyW || gid.y >= u.copyH) return;
    uint sx = uint(u.srcX0) + gid.x;
    uint sy = uint(u.srcY0) + gid.y;
    float yv = clamp(yTex.read(uint2(sx, sy)).r, 0.0, 1.0);
    ulong yOff = ulong(u.dstY0 + int(gid.y)) * ulong(u.dstStrideBytes) +
                 ulong(u.dstX0 + int(gid.x));
    dst[yOff] = uchar(yv * 255.0 + 0.5);
    if ((gid.x & 1u) == 0u && (gid.y & 1u) == 0u) {
        float2 uv = clamp(uvTex.read(uint2(sx >> 1u, sy >> 1u)).rg, 0.0, 1.0);
        uint uvW = (u.copyW + 1u) / 2u;
        ulong yPlaneBytes = ulong(u.dstStrideBytes) * ulong(u.copyH);
        ulong uvOff = yPlaneBytes + ulong(gid.y >> 1u) * ulong(uvW * 2u) + ulong((gid.x >> 1u) * 2u);
        dst[uvOff + 0] = uchar(uv.x * 255.0 + 0.5);
        dst[uvOff + 1] = uchar(uv.y * 255.0 + 0.5);
    }
}
