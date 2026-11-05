/* Owner: filter/filter_functions.h. Every public constructor and extraction
 * form exercised. Pure encoding preserves invalid scalars/reference values;
 * all new operations must reject in CPU submission without touching output.
 * No allocator, owned entries, renderer effects or legal shared state offered.
 * Recipe pool lifecycle belongs to filter_pool_test, not these reference bits. */
#include "filter/filter_functions.h"
#include "lang/filter.h"
#include "compositor/compositor.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

// Asserts unsupported tokens return the documented filter-bounds status.
static void unsupported(FilterToken token) {
    CompositorBounds before = {42, 43, 7, 9};
    CompositorBounds out = before;
    assert(Compositor_filterBounds((CompositorBounds) {0}, &token, 1, &out) == COMPOSITOR_UNSUPPORTED);
    assert(out.x == before.x && out.y == before.y);
    assert(out.width == before.width && out.height == before.height);
    CompositorSurface *surface = nullptr;
    assert(Compositor_compose(nullptr, 0, &token, 1, &surface) == COMPOSITOR_UNSUPPORTED);
    assert(surface == nullptr);
}

// Tests filter constructors, encoded parameters, and unsupported operations.
int main(void) {
    assert(Filter_identity() == 0);
    assert(Filter_gain(1) == UINT64_C(0x000100003f800000));
    assert(Filter_scatterBlur(UINT32_MAX) == UINT64_C(0x00020000ffffffff));
    assert(Filter_id(UINT64_MAX) == UINT16_MAX);
    assert(Filter_payload(UINT64_MAX) == FILTER_PAYLOAD_MASK);
    const float values[] = {0, -0.0f, 1, -1, FLT_MAX, -FLT_MAX, INFINITY, -INFINITY, NAN};
    FilterToken (*scalars[])(float) = {
        Filter_brightness, Filter_contrast, Filter_blackAndWhite
    };
    const uint16_t scalarIds[] = {BRIGHTNESS_ID, CONTRAST_ID, BLACK_AND_WHITE_ID};
    for (size_t i = 0; i < sizeof scalars / sizeof scalars[0]; ++i) {
        for (size_t j = 0; j < sizeof values / sizeof values[0]; ++j) {
            uint32_t bits;
            memcpy(&bits, &values[j], sizeof bits);
            FilterToken token = scalars[i](values[j]);
            assert(Filter_id(token) == scalarIds[i]);
            assert(Filter_payload(token) == bits);
            unsupported(token);
        }
    }
    const uint32_t colors[] = {0, 1, UINT32_C(0x12345678), UINT32_MAX};
    for (size_t i = 0; i < sizeof colors / sizeof colors[0]; ++i) {
        FilterToken token = Filter_monocolor(colors[i]);
        assert(Filter_id(token) == MONOCOLOR_ID);
        assert(Filter_payload(token) == colors[i]);
        unsupported(token);
    }
    const FilterToken empty[] = {Filter_grayscale(), Filter_grayscaleRed(),
        Filter_grayscaleGreen(), Filter_grayscaleBlue(), Filter_invert()};
    const uint16_t emptyIds[] = {GRAYSCALE_ID, GRAYSCALE_RED_ID,
        GRAYSCALE_GREEN_ID, GRAYSCALE_BLUE_ID, INVERT_ID};
    for (size_t i = 0; i < sizeof empty / sizeof empty[0]; ++i) {
        assert(Filter_id(empty[i]) == emptyIds[i]);
        assert(Filter_payload(empty[i]) == 0);
        unsupported(empty[i]);
    }
#define CHECK_REFERENCE(name, id) do { \
    const uint32_t indices[] = {0, 1, UINT32_MAX}; \
    const uint16_t generations[] = {0, 1, UINT16_MAX}; \
    for (size_t i = 0; i < 3; ++i) { \
        for (size_t j = 0; j < 3; ++j) { \
            FilterToken token = Filter_##name(indices[i], generations[j]); \
            assert(Filter_id(token) == id); \
            assert(Filter_payload(token) == ((uint64_t) generations[j] << 32 | indices[i])); \
            unsupported(token); \
        } \
    } \
} while (0)
    CHECK_REFERENCE(toneCurve, TONE_CURVE_ID);
    CHECK_REFERENCE(hsl, HSL_ID);
    CHECK_REFERENCE(hsv, HSV_ID);
    CHECK_REFERENCE(colorBalance, COLOR_BALANCE_ID);
    CHECK_REFERENCE(edges, EDGES_ID);
    CHECK_REFERENCE(dropShadow, DROP_SHADOW_ID);
    CHECK_REFERENCE(gradientMap, GRADIENT_MAP_ID);
    CHECK_REFERENCE(replaceColor, REPLACE_COLOR_ID);
    CHECK_REFERENCE(gaussianBlur, GAUSSIAN_BLUR_ID);
    CHECK_REFERENCE(boxBlur, BOX_BLUR_ID);
    CHECK_REFERENCE(zoomingBlur, ZOOMING_BLUR_ID);
    CHECK_REFERENCE(movingBlur, MOVING_BLUR_ID);
    CHECK_REFERENCE(spinBlur, SPIN_BLUR_ID);
    CHECK_REFERENCE(lensBlur, LENS_BLUR_ID);
    CHECK_REFERENCE(mosaic, MOSAIC_ID);
    CHECK_REFERENCE(unsharpMask, UNSHARP_MASK_ID);
    CHECK_REFERENCE(frostedGlass, FROSTED_GLASS_ID);
    CHECK_REFERENCE(stroke, STROKE_ID);
    CHECK_REFERENCE(stainedGlass, STAINED_GLASS_ID);
    CHECK_REFERENCE(outerGlow, OUTER_GLOW_ID);
    CHECK_REFERENCE(innerGlow, INNER_GLOW_ID);
    CHECK_REFERENCE(emboss, EMBOSS_ID);
    CHECK_REFERENCE(relief, RELIEF_ID);
    CHECK_REFERENCE(waterdrop, WATERDROP_ID);
    CHECK_REFERENCE(extrude, EXTRUDE_ID);
    CHECK_REFERENCE(godRays, GOD_RAYS_ID);
    CHECK_REFERENCE(chromaticAberration, CHROMATIC_ABERRATION_ID);
    CHECK_REFERENCE(glitch, GLITCH_ID);
    CHECK_REFERENCE(noise, NOISE_ID);
    CHECK_REFERENCE(dithering, DITHERING_ID);
    CHECK_REFERENCE(chrome, CHROME_ID);
    CHECK_REFERENCE(bloom, BLOOM_ID);
    CHECK_REFERENCE(sheer, SHEER_ID);
    CHECK_REFERENCE(pixelate, PIXELATE_ID);
    CHECK_REFERENCE(pointillize, POINTILLIZE_ID);
    CHECK_REFERENCE(expansion, EXPANSION_ID);
    CHECK_REFERENCE(fisheye, FISHEYE_ID);
    CHECK_REFERENCE(sphere, SPHERE_ID);
    CHECK_REFERENCE(wave, WAVE_ID);
    CHECK_REFERENCE(dots, DOTS_ID);
#undef CHECK_REFERENCE
    CompositorBounds bounds;
    FilterToken good = Filter_gain(1);
    assert(Compositor_filterBounds((CompositorBounds) {0}, &good, 1, &bounds) == COMPOSITOR_OK);
    puts("filter constructors, reference boundaries and unsupported-state preservation: PASS");
    return 0;
}
