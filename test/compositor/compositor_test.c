/* Owner: compositor/compositor.{h,c}. CPU numeric proof, not GPU proof.
 * Surface inventory: create/destroy/bounds/pixels/constPixels/validate.
 * Operations: filterBounds/compose/sourceOver. Proves origin/expanded support,
 * isolation, painter order, nested groups, fixed scatter versus independent
 * double-precision convolution, alpha/HDR, malformed tokens, cold size/work
 * caps, failure-atomic output and destination, borrowed source lifetime.
 * External synchronization only. Allocation fault injection remains a gap. */
#include "compositor/compositor.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Creates a test surface and asserts that its construction succeeds.
static CompositorSurface *make(CompositorBounds b, const float rgba[4]) {
    CompositorSurface *surface = nullptr;
    assert(CompositorSurface_create(b, &surface) == COMPOSITOR_OK);
    float *p = CompositorSurface_pixels(surface);
    for (size_t i = 0; i < (size_t) b.width * b.height; ++i)
        memcpy(p + i * 4, rgba, 4 * sizeof(float));
    return surface;
}

// Asserts a floating-point result against an explicit tolerance.
static void near(float actual, double expected, double tolerance) {
    assert(isfinite(actual));
    assert(fabs(actual - expected) <= tolerance);
}

// Verifies surface creation, pixel access, limits, and invalid arguments.
static void surface_contract(void) {
    CompositorSurface *zero = CompositorSurface();
    assert(zero && !CompositorSurface_bounds(zero).width);
    CompositorSurface_destroy(zero);
    zero = CompositorSurface_zero();
    assert(zero && !CompositorSurface_bounds(zero).width);
    CompositorSurface_destroy(zero);
    CompositorBounds local = {1, 2, 1, 1};
    zero = CompositorSurface(local);
    assert(zero && CompositorSurface_bounds(zero).x == 1);
    char text[160];
    bool truncated = true;
    CompositorSurface_toString(zero, text, sizeof text, &truncated);
    assert(!truncated && strstr(text, "1 x 1 at 1,2"));
    CompositorSurface_toStringStruct(zero, text, sizeof text, &truncated);
    assert(!truncated && strstr(text, "bounds=") && strstr(text, "pixels="));
    CompositorSurface_toString(nullptr, text, sizeof text, &truncated);
    assert(!truncated && !strcmp(text, "nullptr"));
    CompositorSurface_toStringStruct(nullptr, text, 1, &truncated);
    assert(truncated && !text[0]);
    text[0] = 'x';
    CompositorSurface_toString(zero, text, 0, &truncated);
    assert(truncated && text[0] == 'x');
    CompositorSurface_toStringStruct(zero, nullptr, 10, &truncated);
    assert(truncated);
    CompositorSurface_toString(zero, text, sizeof text, nullptr);
    CompositorSurface_destroy(zero);
    assert(!CompositorSurface_1((CompositorBounds) {0, 0, 0, 1}));
    assert(!CompositorSurface_pixels(nullptr));
    assert(!CompositorSurface_constPixels(nullptr));
    assert(!CompositorSurface_bounds(nullptr).width);
    assert(CompositorSurface_validate(nullptr) == COMPOSITOR_INVALID);
    CompositorSurface_destroy(nullptr);
    CompositorSurface *empty = nullptr;
    assert(CompositorSurface_create((CompositorBounds) {-9, 3, 0, 0}, &empty) == COMPOSITOR_OK);
    assert(CompositorSurface_validate(empty) == COMPOSITOR_OK);
    assert(!CompositorSurface_pixels(empty));
    CompositorSurface *out = empty;
    assert(CompositorSurface_create((CompositorBounds) {0, 0, 0, 1}, &out) == COMPOSITOR_INVALID);
    assert(out == empty);
    assert(CompositorSurface_create((CompositorBounds) {INT32_MAX, 0, 1, 1}, &out) == COMPOSITOR_LIMIT);
    assert(CompositorSurface_create((CompositorBounds) {0, 0, UINT32_MAX, UINT32_MAX}, &out) == COMPOSITOR_LIMIT);
    assert(CompositorSurface_create((CompositorBounds) {0, 0, COMPOSITOR_MAX_PIXELS + 1, 1}, &out) == COMPOSITOR_LIMIT);
    assert(CompositorSurface_create((CompositorBounds) {0, 0, 1, 1}, nullptr) == COMPOSITOR_INVALID);
    const float clear[4] = {0};
    CompositorSurface *one = make((CompositorBounds) {INT32_MIN, -1, 1, 1}, clear);
    float *p = CompositorSurface_pixels(one);
    const float invalid[] = {NAN, INFINITY, -1};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        p[0] = invalid[i];
        assert(CompositorSurface_validate(one) == COMPOSITOR_INVALID);
    }
    p[0] = 1;
    assert(CompositorSurface_validate(one) == COMPOSITOR_INVALID);
    p[3] = 1;
    p[0] = FLT_MAX;
    assert(CompositorSurface_validate(one) == COMPOSITOR_OK);
    p[3] = 1.01f;
    assert(CompositorSurface_validate(one) == COMPOSITOR_INVALID);
    p[3] = -0.0f;
    p[0] = 0;
    assert(CompositorSurface_validate(one) == COMPOSITOR_OK);
    CompositorSurface_destroy(one);
    CompositorSurface_destroy(empty);
}

// Checks filter support bounds and token validation outcomes.
static void support_and_tokens(void) {
    CompositorBounds b = {-10, 8, 2, 3}, out = {100, 101, 4, 5};
    FilterToken stack[] = {Filter_identity(), Filter_gain(2), Filter_scatterBlur(1), Filter_scatterBlur(2)};
    assert(Compositor_filterBounds(b, stack, 4, &out) == COMPOSITOR_OK);
    assert(out.x == -13 && out.y == 5 && out.width == 8 && out.height == 9);
    CompositorBounds previous = out;
    FilterToken bad[] = {1, ((uint64_t) FILTER_GAIN << 48) | (UINT64_C(1) << 32),
        Filter_gain(NAN), Filter_gain(INFINITY), Filter_gain(-1), Filter_scatterBlur(17),
        Filter_scatterBlur(UINT32_MAX), UINT64_C(0x8000000000000000), UINT64_MAX};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        CompositorStatus expected = Filter_id(bad[i]) >= 0x8000 ? COMPOSITOR_UNSUPPORTED : COMPOSITOR_INVALID;
        assert(Compositor_filterBounds(b, bad + i, 1, &out) == expected);
        assert(!memcmp(&previous, &out, sizeof out));
    }
    assert(Compositor_filterBounds(b, nullptr, 1, &out) == COMPOSITOR_INVALID);
    assert(Compositor_filterBounds(b, stack, 1, nullptr) == COMPOSITOR_INVALID);
    assert(Compositor_filterBounds(b, stack, COMPOSITOR_MAX_FILTERS + 1, &out) == COMPOSITOR_LIMIT);
    assert(Compositor_filterBounds((CompositorBounds) {INT32_MIN, 0, 1, 1}, stack + 2, 1, &out) == COMPOSITOR_LIMIT);
    assert(Compositor_filterBounds((CompositorBounds) {INT32_MAX - 1, 0, 1, 1}, stack + 2, 1, &out) == COMPOSITOR_LIMIT);
    FilterToken zero = Filter_gain(-0.0f);
    assert(Compositor_filterBounds(b, &zero, 1, &out) == COMPOSITOR_OK);
    FilterToken maximum = Filter_scatterBlur(FILTER_SCATTER_MAX_RADIUS);
    assert(Compositor_filterBounds(b, &maximum, 1, &out) == COMPOSITOR_OK);
    assert(out.width == 34 && out.height == 35);
    assert(Compositor_filterBounds((CompositorBounds) {0}, bad + 7, 1, &out) == COMPOSITOR_UNSUPPORTED);
    assert(Compositor_filterBounds((CompositorBounds) {0, 0, 257, 257}, &maximum, 1, &out) == COMPOSITOR_LIMIT);
    FilterToken identities[COMPOSITOR_MAX_FILTERS];
    for (size_t i = 0; i < COMPOSITOR_MAX_FILTERS; ++i)
        identities[i] = Filter_identity();
    assert(Compositor_filterBounds(b, identities, COMPOSITOR_MAX_FILTERS, &out) == COMPOSITOR_OK);
}

// Tests isolated surface composition and source-over color results.
static void isolation_and_over(void) {
    const float red[4] = {.5f, 0, 0, .5f}, blue[4] = {0, 0, .5f, .5f};
    CompositorSurface *a = make((CompositorBounds) {-2, 4, 1, 1}, red);
    CompositorSurface *b = make((CompositorBounds) {-2, 4, 1, 1}, blue);
    const CompositorSurface *sources[] = {a, b};
    FilterToken gain = Filter_gain(2);
    CompositorSurface *group = nullptr;
    assert(Compositor_compose(sources, 2, &gain, 1, &group) == COMPOSITOR_OK);
    const float *p = CompositorSurface_constPixels(group);
    near(p[0], .5, 0); near(p[2], 1, 0); near(p[3], .75, 0);
    assert(CompositorSurface_bounds(group).x == -2);
    assert(CompositorSurface_constPixels(a)[0] == .5f);
    CompositorSurface *blurredGroup = nullptr;
    FilterToken blur = Filter_scatterBlur(1);
    assert(Compositor_compose(sources, 2, &blur, 1, &blurredGroup) == COMPOSITOR_OK);
    /* Filter assembled alpha .75 exactly once: individually filtering the two
     * children would instead give 1-(1-.5/9)^2 at this halo pixel. */
    near(CompositorSurface_constPixels(blurredGroup)[3], .75 / 9, 1e-7);
    near(CompositorSurface_constPixels(blurredGroup)[0], .25 / 9, 1e-7);
    near(CompositorSurface_constPixels(blurredGroup)[2], .5 / 9, 1e-7);
    CompositorSurface_destroy(blurredGroup);
    const CompositorSurface *nested[] = {group};
    FilterToken halve = Filter_gain(.5f);
    CompositorSurface *result = nullptr;
    assert(Compositor_compose(nested, 1, &halve, 1, &result) == COMPOSITOR_OK);
    near(CompositorSurface_constPixels(result)[0], .25, 0);
    near(CompositorSurface_constPixels(result)[2], .5, 0);
    CompositorSurface_destroy(group);
    group = nullptr;
    const CompositorSurface *reversed[] = {b, a};
    assert(Compositor_compose(reversed, 2, nullptr, 0, &group) == COMPOSITOR_OK);
    near(CompositorSurface_constPixels(group)[0], .5, 0);
    near(CompositorSurface_constPixels(group)[2], .25, 0);
    const float clear[4] = {0};
    CompositorSurface *dest = make((CompositorBounds) {-3, 4, 2, 1}, clear);
    assert(Compositor_sourceOver(result, dest) == COMPOSITOR_OK);
    near(CompositorSurface_constPixels(dest)[0], 0, 0);
    near(CompositorSurface_constPixels(dest)[4], .25, 0);
    assert(Compositor_sourceOver(dest, dest) == COMPOSITOR_INVALID);
    assert(Compositor_sourceOver(nullptr, dest) == COMPOSITOR_INVALID);
    assert(Compositor_sourceOver(dest, nullptr) == COMPOSITOR_INVALID);
    CompositorSurface *unchanged = result;
    assert(Compositor_compose(nullptr, 1, nullptr, 0, &unchanged) == COMPOSITOR_INVALID);
    assert(Compositor_compose(sources, 2, nullptr, 1, &unchanged) == COMPOSITOR_INVALID);
    assert(Compositor_compose(sources, COMPOSITOR_MAX_SOURCES + 1, nullptr, 0, &unchanged) == COMPOSITOR_LIMIT);
    assert(Compositor_compose(sources, 2, nullptr, 0, nullptr) == COMPOSITOR_INVALID);
    const CompositorSurface *nullSource[] = {nullptr};
    assert(Compositor_compose(nullSource, 1, nullptr, 0, &unchanged) == COMPOSITOR_INVALID);
    assert(unchanged == result);
    CompositorSurface *empty = nullptr;
    assert(Compositor_compose(nullptr, 0, nullptr, 0, &empty) == COMPOSITOR_OK);
    assert(!CompositorSurface_bounds(empty).width);
    const CompositorSurface *many[COMPOSITOR_MAX_SOURCES];
    for (size_t i = 0; i < COMPOSITOR_MAX_SOURCES; ++i)
        many[i] = empty;
    CompositorSurface *manyOut = nullptr;
    assert(Compositor_compose(many, COMPOSITOR_MAX_SOURCES, nullptr, 0, &manyOut) == COMPOSITOR_OK);
    CompositorSurface_destroy(manyOut);
    CompositorSurface_destroy(empty);
    CompositorSurface_destroy(dest);
    CompositorSurface_destroy(result);
    CompositorSurface_destroy(group);
    CompositorSurface_destroy(a);
    CompositorSurface_destroy(b);
}

// Verifies bounds union behavior and arithmetic/resource limits.
static void union_and_limits(void) {
    const float red[4] = {1, 0, 0, 1}, blue[4] = {0, 0, 1, 1};
    CompositorSurface *a = make((CompositorBounds) {-2, -3, 1, 1}, red);
    CompositorSurface *b = make((CompositorBounds) {0, -2, 1, 1}, blue);
    const CompositorSurface *sources[] = {a, b};
    CompositorSurface *out = nullptr;
    assert(Compositor_compose(sources, 2, nullptr, 0, &out) == COMPOSITOR_OK);
    CompositorBounds bounds = CompositorSurface_bounds(out);
    assert(bounds.x == -2 && bounds.y == -3 && bounds.width == 3 && bounds.height == 2);
    const float *p = CompositorSurface_constPixels(out);
    assert(p[0] == 1 && p[3] == 1 && p[7] == 0);
    assert(p[4 * 5 + 2] == 1 && p[4 * 5 + 3] == 1);
    CompositorSurface_destroy(out);
    CompositorSurface_destroy(a);
    CompositorSurface_destroy(b);
    a = make((CompositorBounds) {INT32_MIN, 0, 1, 1}, red);
    b = make((CompositorBounds) {INT32_MAX - 1, 0, 1, 1}, blue);
    sources[0] = a; sources[1] = b;
    out = a;
    assert(Compositor_compose(sources, 2, nullptr, 0, &out) == COMPOSITOR_LIMIT);
    assert(out == a);
    CompositorSurface_destroy(a);
    CompositorSurface_destroy(b);
    const float clear[4] = {0};
    a = make((CompositorBounds) {0, 0, 257, 257}, clear);
    sources[0] = a;
    FilterToken maximum = Filter_scatterBlur(16);
    assert(Compositor_compose(sources, 1, &maximum, 1, &out) == COMPOSITOR_LIMIT);
    CompositorSurface_destroy(a);
}

/* Independent double-precision convolution oracle. The implementation under
 * test SCATTERS; this oracle enumerates destination neighborhoods solely to
 * avoid reproducing implementation indexing and accumulation order. */
static void check_scatter(const CompositorSurface *src, uint32_t radius) {
    const CompositorSurface *sources[] = {src};
    FilterToken blur = Filter_scatterBlur(radius);
    CompositorSurface *out = nullptr;
    assert(Compositor_compose(sources, 1, &blur, 1, &out) == COMPOSITOR_OK);
    CompositorBounds s = CompositorSurface_bounds(src), d = CompositorSurface_bounds(out);
    assert((int64_t) d.x == (int64_t) s.x - radius);
    assert((int64_t) d.y == (int64_t) s.y - radius);
    assert(d.width == s.width + 2 * radius && d.height == s.height + 2 * radius);
    const float *p = CompositorSurface_constPixels(src), *q = CompositorSurface_constPixels(out);
    uint32_t diameter = radius * 2 + 1;
    double mass[4] = {0}, expectedMass[4] = {0};
    for (uint32_t y = 0; y < d.height; ++y) {
        for (uint32_t x = 0; x < d.width; ++x) {
            double expected[4] = {0};
            for (uint32_t sy = 0; sy < s.height; ++sy) {
                for (uint32_t sx = 0; sx < s.width; ++sx) {
                    int64_t dx = (int64_t) x - radius - sx, dy = (int64_t) y - radius - sy;
                    if (dx < -(int64_t) radius || dx > radius || dy < -(int64_t) radius || dy > radius)
                        continue;
                    for (unsigned c = 0; c < 4; ++c)
                        expected[c] += p[4 * ((size_t) sy * s.width + sx) + c] /
                            ((double) diameter * diameter);
                }
            }
            for (unsigned c = 0; c < 4; ++c) {
                float actual = q[4 * ((size_t) y * d.width + x) + c];
                /* Float sum of <=1089 positive terms: conservative precision
                 * bound scaled to result; independent oracle uses double. */
                near(actual, expected[c], 4e-5 * fmax(1.0, expected[c]));
                mass[c] += actual;
            }
        }
    }
    for (size_t i = 0; i < (size_t) s.width * s.height; ++i)
        for (unsigned c = 0; c < 4; ++c)
            expectedMass[c] += p[4 * i + c];
    for (unsigned c = 0; c < 4; ++c)
        assert(fabs(mass[c] - expectedMass[c]) <= 4e-5 * fmax(1.0, expectedMass[c]));
    assert(CompositorSurface_validate(out) == COMPOSITOR_OK);
    CompositorSurface_destroy(out);
}

// Exercises scatter kernels on impulse and constant-field fixtures.
static void scatter_oracles(void) {
    const float impulse[4] = {.25f, .125f, 0, .5f};
    CompositorSurface *one = make((CompositorBounds) {-8, 19, 1, 1}, impulse);
    check_scatter(one, 0);
    check_scatter(one, 1);
    check_scatter(one, FILTER_SCATTER_MAX_RADIUS);
    const float constant[4] = {.3f, .6f, 1.2f, 1};
    CompositorSurface *square = make((CompositorBounds) {7, -11, 5, 5}, constant);
    check_scatter(square, 1);
    const CompositorSurface *sources[] = {square};
    FilterToken blur = Filter_scatterBlur(1);
    CompositorSurface *out = nullptr;
    assert(Compositor_compose(sources, 1, &blur, 1, &out) == COMPOSITOR_OK);
    const float *p = CompositorSurface_constPixels(out);
    near(p[4 * (3 * 7 + 3)], .3, 1e-6);
    near(p[3], 1.0 / 9, 1e-7); /* halo: no edge renormalization */
    CompositorSurface_destroy(out);
    float *input = CompositorSurface_pixels(square);
    for (size_t i = 0; i < 25; ++i) {
        float alpha = (float) (i % 5) / 4;
        input[4 * i] = alpha * (float) (i + 1) / 25;
        input[4 * i + 1] = alpha * .25f;
        input[4 * i + 2] = alpha * .5f;
        input[4 * i + 3] = alpha;
    }
    check_scatter(square, 2);
    FilterToken twice[] = {blur, blur};
    assert(Compositor_compose(sources, 1, twice, 2, &out) == COMPOSITOR_OK);
    assert(CompositorSurface_bounds(out).x == 5);
    assert(CompositorSurface_bounds(out).width == 9);
    CompositorSurface_destroy(out);
    CompositorSurface_destroy(square);
    CompositorSurface_destroy(one);
}

// Confirms overflow rejection preserves source and destination surfaces.
static void overflow_atomicity(void) {
    const float huge[4] = {FLT_MAX, 0, 0, .5f};
    CompositorSurface *src = make((CompositorBounds) {0, 0, 1, 1}, huge);
    CompositorSurface *dst = make((CompositorBounds) {0, 0, 1, 1}, huge);
    float before[4];
    memcpy(before, CompositorSurface_constPixels(dst), sizeof before);
    assert(Compositor_sourceOver(src, dst) == COMPOSITOR_LIMIT);
    assert(!memcmp(before, CompositorSurface_constPixels(dst), sizeof before));
    const CompositorSurface *sources[] = {src};
    FilterToken ordered[] = {Filter_gain(2), Filter_gain(0)};
    CompositorSurface *out = dst;
    assert(Compositor_compose(sources, 1, ordered, 2, &out) == COMPOSITOR_LIMIT);
    assert(out == dst && !memcmp(before, CompositorSurface_constPixels(src), sizeof before));
    FilterToken reversed[] = {ordered[1], ordered[0]};
    assert(Compositor_compose(sources, 1, reversed, 2, &out) == COMPOSITOR_OK);
    near(CompositorSurface_constPixels(out)[0], 0, 0);
    CompositorSurface_destroy(out);
    FilterToken blur = Filter_scatterBlur(1);
    assert(Compositor_compose(sources, 1, &blur, 1, &out) == COMPOSITOR_OK);
    assert(CompositorSurface_validate(out) == COMPOSITOR_OK);
    CompositorSurface_destroy(out);
    CompositorSurface_pixels(src)[0] = NAN;
    out = dst;
    assert(Compositor_compose(sources, 1, nullptr, 0, &out) == COMPOSITOR_INVALID);
    assert(Compositor_sourceOver(src, dst) == COMPOSITOR_INVALID);
    assert(!memcmp(before, CompositorSurface_constPixels(dst), sizeof before));
    CompositorSurface_destroy(src);
    CompositorSurface_destroy(dst);
}

// Runs compositor surface, filtering, scatter, and atomicity contracts.
int main(void) {
    surface_contract();
    support_and_tokens();
    isolation_and_over();
    union_and_limits();
    scatter_oracles();
    overflow_atomicity();
    puts("compositor CPU contracts and scatter numeric oracles: PASS");
    return 0;
}
