#version 450
#extension GL_GOOGLE_include_directive : require
#include "filter/filter_type.h"

// One ordered pointwise pass over an isolated linear-premultiplied group.
// Reuse resolve.vert's fullscreen triangle. Binding 0 is a separate completed
// source image, never the current attachment. Host validates ID/scalar once,
// supplies matching native-pixel extents, and establishes read/write barriers.
// No blending here: write the filtered group; source-over happens only when
// composing the finished group. Foreground/backdrop/element share this pass.
layout(set = 0, binding = 0) uniform sampler2D sourceColor;
layout(push_constant) uniform ColorPush {
    uint operation;
    uint scalarBits;
} pc;
layout(location = 0) out vec4 filteredColor;

// Fixed Rec.709 colorimetry, not style defaults. Premultiplied formulas avoid
// dividing by tiny alpha; saturation before contrast multiplication prevents
// overflow without requiring f3264 (not available on the baseline GPU).
const vec3 REC709 = vec3(0.2126, 0.7152, 0.0722);
const f32 MAX_FINITE = 3.402823466e38;

f32 contrastChannel(f32 value, f32 alpha, f32 amount) {
    f32 pivot = 0.5 * alpha;
    f32 delta = value - pivot;
    if (amount == 0.0 || delta == 0.0)
        return pivot;
    if (amount > 1.0) {
        f32 limit = pivot / amount;
        if (delta > 0.0 && delta >= limit)
            return alpha;
        if (delta < 0.0 && -delta >= limit)
            return 0.0;
    }
    return clamp(delta * amount + pivot, 0.0, alpha);
}

void main() {
    vec4 color = texelFetch(sourceColor, ivec2(gl_FragCoord.xy), 0);
    f32 alpha = color.a;
    if (alpha == 0.0) {
        filteredColor = vec4(0.0);
        return;
    }
    f32 amount = uintBitsToFloat(pc.scalarBits);
    switch (pc.operation) {
    case BRIGHTNESS_ID:
        color.rgb = clamp(color.rgb + amount * alpha, vec3(0.0), vec3(alpha));
        break;
    case CONTRAST_ID: {
        color.rgb = vec3(contrastChannel(color.r, alpha, amount),
                         contrastChannel(color.g, alpha, amount),
                         contrastChannel(color.b, alpha, amount));
        break;
    }
    case INVERT_ID:
        color.rgb = clamp(vec3(alpha) - color.rgb, vec3(0.0), vec3(alpha));
        break;
    case GRAYSCALE_RED_ID:
        color.rgb = color.rrr;
        break;
    case GRAYSCALE_GREEN_ID:
        color.rgb = color.ggg;
        break;
    case GRAYSCALE_BLUE_ID:
        color.rgb = color.bbb;
        break;
    case GRAYSCALE_ID:
        color.rgb = vec3(min(dot(color.rgb, REC709), MAX_FINITE));
        break;
    case BLACK_AND_WHITE_ID:
        color.rgb = vec3(dot(color.rgb, REC709) >= amount * alpha ? alpha : 0.0);
        break;
    }
    filteredColor = color;
}
