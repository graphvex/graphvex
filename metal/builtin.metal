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
