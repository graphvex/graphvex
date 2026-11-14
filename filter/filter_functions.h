#ifndef GRAPHVEX_FILTER_FUNCTIONS_H
#define GRAPHVEX_FILTER_FUNCTIONS_H

#include <stdint.h>
#include <string.h>
#include <f32.h>
#include "filter/filter_type.h"

/* Header-only token vocabulary, not an owning object or allocator.
 * Numeric ABI: ID16 | payload48. No pointers. Serialize with explicit endian.
 * Constructors only encode arguments, never allocate, validate, retain or edit.
 * Invalid arguments are preserved for cold submission rejection. Caller supplies
 * originating pool context for references; an index/generation is NOT global.
 * CPU reference executes only identity/gain/scatterBlur; new color operations
 * belong to the Vulkan shader pass, never a CPU fallback.
 * All functions are pure/thread-independent; token copies do not acquire owners.
 */
typedef u64 FilterToken;
#define FILTER_PAYLOAD_MASK UINT64_C(0x0000ffffffffffff)
/* Existing CPU prototype work bound; not a semantic maximum for future blur. */
#define FILTER_SCATTER_MAX_RADIUS 16u

_Static_assert(sizeof(FilterToken) == 8, "filter token ABI");
_Static_assert(sizeof(f32) == 4, "scalar payload requires binary32");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "scalar payload requires IEEE binary32");

static inline u16 Filter_id(FilterToken token) {
    return (u16) (token >> 48);
}
static inline u64 Filter_payload(FilterToken token) {
    return token & FILTER_PAYLOAD_MASK;
}
static inline FilterToken Filter_identity() { return 0; }
/* Private encoding helpers: no validity or ownership assertion. */
static inline FilterToken filterScalar(u16 id, f32 value) {
    u32 bits;
    memcpy(&bits, &value, sizeof bits);
    return ((u64) id << 48) | bits;
}
static inline FilterToken filterReference(u16 id, u32 index,
                                          u16 generation) {
    return ((u64) id << 48) | ((u64) generation << 32) | index;
}
/* Gain scales linear premultiplied RGB, preserves alpha and permits HDR.
 * CPU submission requires finite nonnegative gain, reserved bits zero. */
static inline FilterToken Filter_gain(f32 gain) {
    return filterScalar(GAIN_ID, gain);
}
/* Uniform square scatter: transparent outside, full kernel denominator.
 * Zero is identity. Values above the prototype radius bound remain encoded. */
static inline FilterToken Filter_scatterBlur(u32 radius) {
    return ((u64) SCATTER_BLUR_ID << 48) | radius;
}
/* GPU straight-linear color: brightness finite [-1,1] additive amount;
 * contrast finite >=0 multiplier around 0.5; both clamp RGB to [0,1].
 * B&W finite [0,1] threshold against Rec.709 luminance; equality selects white.
 * All preserve alpha. Constructors preserve invalid input for cold rejection. */
static inline FilterToken Filter_brightness(f32 amount) {
    return filterScalar(BRIGHTNESS_ID, amount);
}
static inline FilterToken Filter_contrast(f32 amount) {
    return filterScalar(CONTRAST_ID, amount);
}
static inline FilterToken Filter_blackAndWhite(f32 threshold) {
    return filterScalar(BLACK_AND_WHITE_ID, threshold);
}
/* Packed color is 0xRRGGBBAA; low 32 payload bits, upper 16 reserved zero. */
static inline FilterToken Filter_monocolor(u32 color) {
    return ((u64) MONOCOLOR_ID << 48) | color;
}
static inline FilterToken Filter_grayscale() {
    return (u64) GRAYSCALE_ID << 48;
}
static inline FilterToken Filter_grayscaleRed() {
    return (u64) GRAYSCALE_RED_ID << 48;
}
static inline FilterToken Filter_grayscaleGreen() {
    return (u64) GRAYSCALE_GREEN_ID << 48;
}
static inline FilterToken Filter_grayscaleBlue() {
    return (u64) GRAYSCALE_BLUE_ID << 48;
}
static inline FilterToken Filter_invert() {
    return (u64) INVERT_ID << 48;
}
/* Parameter reference payload: generation16 | index32. Generation zero encodes
 * an invalid reference; it does not turn into identity. These functions accept
 * an already acquired entry, NOT effect parameters. Typed parameter allocation,
 * validation, retain/release, copy-on-write and migration remain future work.
 * Modes (e.g. extrude parallel/perspective, pixelate shape) belong in that record.
 */
#define GRAPHVEX_FILTER_REFERENCE(name, id) \
    static inline FilterToken Filter_##name(u32 index, u16 generation) { \
        return filterReference(id, index, generation); \
    }
GRAPHVEX_FILTER_REFERENCE(toneCurve, TONE_CURVE_ID)
GRAPHVEX_FILTER_REFERENCE(hsl, HSL_ID)
GRAPHVEX_FILTER_REFERENCE(hsv, HSV_ID)
GRAPHVEX_FILTER_REFERENCE(colorBalance, COLOR_BALANCE_ID)
GRAPHVEX_FILTER_REFERENCE(edges, EDGES_ID)
GRAPHVEX_FILTER_REFERENCE(dropShadow, DROP_SHADOW_ID)
GRAPHVEX_FILTER_REFERENCE(gradientMap, GRADIENT_MAP_ID)
GRAPHVEX_FILTER_REFERENCE(replaceColor, REPLACE_COLOR_ID)
GRAPHVEX_FILTER_REFERENCE(gaussianBlur, GAUSSIAN_BLUR_ID)
GRAPHVEX_FILTER_REFERENCE(boxBlur, BOX_BLUR_ID)
GRAPHVEX_FILTER_REFERENCE(zoomingBlur, ZOOMING_BLUR_ID)
GRAPHVEX_FILTER_REFERENCE(movingBlur, MOVING_BLUR_ID)
GRAPHVEX_FILTER_REFERENCE(spinBlur, SPIN_BLUR_ID)
GRAPHVEX_FILTER_REFERENCE(lensBlur, LENS_BLUR_ID)
GRAPHVEX_FILTER_REFERENCE(mosaic, MOSAIC_ID)
GRAPHVEX_FILTER_REFERENCE(unsharpMask, UNSHARP_MASK_ID)
GRAPHVEX_FILTER_REFERENCE(frostedGlass, FROSTED_GLASS_ID)
GRAPHVEX_FILTER_REFERENCE(stroke, STROKE_ID)
GRAPHVEX_FILTER_REFERENCE(stainedGlass, STAINED_GLASS_ID)
GRAPHVEX_FILTER_REFERENCE(outerGlow, OUTER_GLOW_ID)
GRAPHVEX_FILTER_REFERENCE(innerGlow, INNER_GLOW_ID)
GRAPHVEX_FILTER_REFERENCE(emboss, EMBOSS_ID)
GRAPHVEX_FILTER_REFERENCE(relief, RELIEF_ID)
GRAPHVEX_FILTER_REFERENCE(waterdrop, WATERDROP_ID)
GRAPHVEX_FILTER_REFERENCE(extrude, EXTRUDE_ID)
GRAPHVEX_FILTER_REFERENCE(godRays, GOD_RAYS_ID)
GRAPHVEX_FILTER_REFERENCE(chromaticAberration, CHROMATIC_ABERRATION_ID)
GRAPHVEX_FILTER_REFERENCE(glitch, GLITCH_ID)
GRAPHVEX_FILTER_REFERENCE(noise, NOISE_ID)
GRAPHVEX_FILTER_REFERENCE(dithering, DITHERING_ID)
GRAPHVEX_FILTER_REFERENCE(chrome, CHROME_ID)
GRAPHVEX_FILTER_REFERENCE(bloom, BLOOM_ID)
GRAPHVEX_FILTER_REFERENCE(sheer, SHEER_ID)
GRAPHVEX_FILTER_REFERENCE(pixelate, PIXELATE_ID)
GRAPHVEX_FILTER_REFERENCE(pointillize, POINTILLIZE_ID)
GRAPHVEX_FILTER_REFERENCE(expansion, EXPANSION_ID)
GRAPHVEX_FILTER_REFERENCE(fisheye, FISHEYE_ID)
GRAPHVEX_FILTER_REFERENCE(sphere, SPHERE_ID)
GRAPHVEX_FILTER_REFERENCE(wave, WAVE_ID)
GRAPHVEX_FILTER_REFERENCE(dots, DOTS_ID)
#undef GRAPHVEX_FILTER_REFERENCE

#endif
