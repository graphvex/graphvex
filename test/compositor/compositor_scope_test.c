/* Owner: compositor/compositor_scope.{h,c}. Public inventory: crop/scopedScene.
 * CPU numeric contract: exact-prefix backdrop replacement, isolated foreground
 * blur clipped BEFORE assembly, whole-group blur AFTER assembly and spilling
 * outside panel, viewport/origin preservation, nested sequential panels, masks
 * on all premult channels, rejection/failure-atomic outputs, borrowed lifetime.
 * Rectangle-only opaque-prefix prototype. No GPU/window/appearance claims.
 * Allocation fault injection and rounded/translucent scenes remain gaps. */
#include "../../compositor/compositor_scope.h"

#include <assert.h>
#include <f32.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Creates a test surface initialized to the supplied pixel color.
static CompositorSurface *makeSurface(CompositorBounds bounds, const f32 rgba[4]) {
    CompositorSurface *surface = nullptr;
    assert(CompositorSurface_create(bounds, &surface) == COMPOSITOR_OK);
    f32 *p = CompositorSurface_pixels(surface);
    for (bytes i = 0; i < (bytes) bounds.width * bounds.height; ++i)
        memcpy(p + 4 * i, rgba, 4 * sizeof(f32));
    return surface;
}

// Returns the pixel pointer when coordinates are within the surface bounds.
static const f32 *at(const CompositorSurface *surface, i32 x, i32 y) {
    CompositorBounds b = CompositorSurface_bounds(surface);
    assert(x >= b.x && y >= b.y);
    assert((int64_t) x < (int64_t) b.x + b.width && (int64_t) y < (int64_t) b.y + b.height);
    return CompositorSurface_constPixels(surface) +
        4 * ((bytes) ((int64_t) y - b.y) * b.width + (bytes) ((int64_t) x - b.x));
}

// Asserts that a f32 result is within the test's numeric tolerance.
static fn near(f32 actual, d64 expected) {
    assert(isfinite(actual) && fabs(actual - expected) < 2e-6);
}

// Verifies crop bounds, pixels, and rejected-output preservation.
static fn cropContract() {
    const f32 translucent[4] = {.25f, .125f, 0, .5f};
    CompositorSurface *source = makeSurface((CompositorBounds) {-3, 7, 2, 2}, translucent);
    CompositorSurface *crop = nullptr;
    assert(Compositor_crop(source, (CompositorBounds) {-4, 6, 4, 4}, &crop) == COMPOSITOR_OK);
    CompositorBounds bounds = CompositorSurface_bounds(crop);
    assert(bounds.x == -4 && bounds.y == 6 && bounds.width == 4 && bounds.height == 4);
    for (i32 y = 6; y < 10; ++y) {
        for (i32 x = -4; x < 0; ++x) {
            const f32 *p = at(crop, x, y);
            bool inside = x >= -3 && x < -1 && y >= 7 && y < 9;
            for (unsigned c = 0; c < 4; ++c)
                near(p[c], inside ? translucent[c] : 0);
        }
    }
    CompositorSurface *unchanged = crop;
    assert(Compositor_crop(nullptr, bounds, &unchanged) == COMPOSITOR_INVALID);
    assert(Compositor_crop(source, bounds, nullptr) == COMPOSITOR_INVALID);
    assert(Compositor_crop(source, (CompositorBounds) {0, 0, 0, 1}, &unchanged) == COMPOSITOR_INVALID);
    assert(Compositor_crop(source, (CompositorBounds) {INT32_MAX, 0, 1, 1}, &unchanged) == COMPOSITOR_LIMIT);
    assert(unchanged == crop);
    CompositorSurface *empty = nullptr;
    assert(Compositor_crop(source, (CompositorBounds) {0}, &empty) == COMPOSITOR_OK);
    assert(!CompositorSurface_bounds(empty).width);
    CompositorSurface_destroy(empty);
    CompositorSurface *outside = nullptr;
    assert(Compositor_crop(source, (CompositorBounds) {100, -100, 1, 1}, &outside) == COMPOSITOR_OK);
    assert(at(outside, 100, -100)[3] == 0);
    CompositorSurface_destroy(outside);
    CompositorSurface_destroy(crop);
    assert(CompositorSurface_validate(source) == COMPOSITOR_OK);
    CompositorSurface_destroy(source);
}

// Checks backdrop filtering replaces the prior scene region exactly once.
static fn backdropReplacement() {
    const f32 black[4] = {0, 0, 0, 1};
    CompositorSurface *prior = makeSurface((CompositorBounds) {-4, -4, 9, 9}, black);
    f32 *p = CompositorSurface_pixels(prior);
    for (u32 y = 0; y < 9; ++y)
        for (u32 x = 0; x < 9; ++x)
            p[4 * (y * 9 + x)] = (x + y) % 2 ? 0 : 1;
    f32 original[9 * 9 * 4];
    memcpy(original, p, sizeof original);
    FilterToken blur = Filter_scatterBlur(1);
    CompositorScopeDesc desc = {
        .priorScene = prior, .panelBounds = {-2, -2, 5, 5},
        .backdropFilters = &blur, .backdropFilterCount = 1
    };
    CompositorSurface *scene = nullptr;
    assert(Compositor_scopedScene(&desc, &scene) == COMPOSITOR_OK);
    CompositorBounds bounds = CompositorSurface_bounds(scene);
    assert(bounds.x == -4 && bounds.y == -4 && bounds.width == 9 && bounds.height == 9);
    for (i32 y = -4; y <= 4; ++y) {
        for (i32 x = -4; x <= 4; ++x) {
            const f32 *q = at(scene, x, y);
            bool inPanel = x >= -2 && x <= 2 && y >= -2 && y <= 2;
            d64 expected = inPanel ? ((x + y) % 2 ? 4.0 / 9 : 5.0 / 9) : at(prior, x, y)[0];
            near(q[0], expected);
            assert(q[3] == 1); /* no prior alpha doubling */
        }
    }
    assert(!memcmp(original, CompositorSurface_constPixels(prior), sizeof original));
    CompositorSurface_destroy(scene);
    /* Foreground and decoration are SHARP over the independently blurred
     * backdrop: neither is visible to backdrop sampling. Element gain runs LAST. */
    const f32 yellow[4] = {.25f, .25f, 0, .5f}, blue[4] = {0, 0, .5f, .5f};
    CompositorSurface *decoration = makeSurface(desc.panelBounds, yellow);
    CompositorSurface *foreground = makeSurface((CompositorBounds) {0, 0, 1, 1}, blue);
    const CompositorSurface *sources[] = {foreground};
    FilterToken gain = Filter_gain(2);
    desc.decoration = decoration;
    desc.foreground = sources;
    desc.foregroundCount = 1;
    desc.elementFilters = &gain;
    desc.elementFilterCount = 1;
    assert(Compositor_scopedScene(&desc, &scene) == COMPOSITOR_OK);
    const f32 *q = at(scene, 0, 0);
    near(q[0], 2 * (.125 + .25 * (5.0 / 9)));
    near(q[1], .25);
    near(q[2], 1);
    near(q[3], 1);
    q = at(scene, 1, 0);
    near(q[0], 2 * (.25 + .5 * (4.0 / 9)));
    near(q[1], .5);
    near(q[2], 0);
    near(at(scene, 3, 0)[0], at(prior, 3, 0)[0]);
    CompositorSurface_destroy(scene);
    CompositorSurface_destroy(decoration);
    CompositorSurface_destroy(foreground);
    CompositorSurface_destroy(prior);
}

// Checks foreground clipping and element-filter spill beyond placement.
static fn foregroundClipAndElementSpill() {
    const f32 black[4] = {0, 0, 0, 1}, green[4] = {0, 1, 0, 1}, red[4] = {1, 0, 0, 1};
    CompositorSurface *prior = makeSurface((CompositorBounds) {0, 0, 9, 9}, black);
    CompositorSurface *decoration = makeSurface((CompositorBounds) {2, 2, 5, 5}, green);
    CompositorSurface *child = makeSurface((CompositorBounds) {2, 4, 1, 1}, red);
    const CompositorSurface *sources[] = {child};
    FilterToken blur = Filter_scatterBlur(1);
    CompositorScopeDesc desc = {
        .priorScene = prior, .decoration = decoration,
        .foreground = sources, .foregroundCount = 1, .panelBounds = {2, 2, 5, 5},
        .foregroundFilters = &blur, .foregroundFilterCount = 1
    };
    CompositorSurface *scene = nullptr;
    assert(Compositor_scopedScene(&desc, &scene) == COMPOSITOR_OK);
    near(at(scene, 2, 4)[0], 1.0 / 9);
    near(at(scene, 2, 4)[1], 8.0 / 9);
    near(at(scene, 1, 4)[0], 0); /* child blur cropped before assembling */
    near(at(scene, 1, 4)[1], 0);
    near(at(scene, 6, 4)[1], 1); /* decoration never foreground-blurred */
    CompositorSurface_destroy(scene);
    desc.foregroundFilters = nullptr;
    desc.foregroundFilterCount = 0;
    desc.elementFilters = &blur;
    desc.elementFilterCount = 1;
    assert(Compositor_scopedScene(&desc, &scene) == COMPOSITOR_OK);
    near(at(scene, 1, 4)[0], 1.0 / 9); /* entire group blur may spill */
    near(at(scene, 1, 4)[1], 2.0 / 9);
    near(at(scene, 7, 4)[1], 1.0 / 3);
    near(at(scene, 0, 4)[1], 0);
    assert(at(scene, 1, 4)[3] == 1);
    /* Sequential scopes see the exact previous output, not old/global scene. */
    FilterToken gain = Filter_gain(.5f);
    CompositorScopeDesc next = {
        .priorScene = scene, .panelBounds = {1, 3, 2, 3},
        .backdropFilters = &gain, .backdropFilterCount = 1
    };
    CompositorSurface *nextScene = nullptr;
    assert(Compositor_scopedScene(&next, &nextScene) == COMPOSITOR_OK);
    near(at(nextScene, 1, 4)[0], 1.0 / 18);
    near(at(nextScene, 1, 4)[1], 1.0 / 9);
    near(at(nextScene, 7, 4)[1], 1.0 / 3);
    CompositorSurface_destroy(nextScene);
    CompositorSurface_destroy(scene);
    CompositorSurface_destroy(child);
    CompositorSurface_destroy(decoration);
    CompositorSurface_destroy(prior);
}

// Ensures foreground-only composition does not capture prior scene pixels.
static fn noBackdropDoesNotBakePrior() {
    const f32 red[4] = {1, 0, 0, 1};
    CompositorSurface *prior = makeSurface((CompositorBounds) {0, 0, 7, 7}, red);
    FilterToken blur = Filter_scatterBlur(1);
    CompositorScopeDesc desc = {
        .priorScene = prior, .panelBounds = {2, 2, 3, 3},
        .elementFilters = &blur, .elementFilterCount = 1
    };
    CompositorSurface *scene = nullptr;
    assert(Compositor_scopedScene(&desc, &scene) == COMPOSITOR_OK);
    for (bytes i = 0; i < 7 * 7 * 4; ++i)
        assert(CompositorSurface_constPixels(scene)[i] == CompositorSurface_constPixels(prior)[i]);
    CompositorSurface_destroy(scene);
    CompositorSurface_destroy(prior);
}

// Verifies invalid scope inputs reject without changing prior content.
static fn rejections() {
    const f32 gray[4] = {.25f, .25f, .25f, 1};
    CompositorSurface *prior = makeSurface((CompositorBounds) {0, 0, 7, 7}, gray);
    CompositorScopeDesc desc = {.priorScene = prior, .panelBounds = {2, 2, 3, 3}};
    CompositorSurface *out = prior;
    assert(Compositor_scopedScene(nullptr, &out) == COMPOSITOR_INVALID);
    assert(Compositor_scopedScene(&desc, nullptr) == COMPOSITOR_INVALID);
    desc.radius = 1;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_UNSUPPORTED);
    desc.radius = 0;
    desc.panelBounds.x = -1;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_LIMIT);
    desc.panelBounds = (CompositorBounds) {2, 2, 0, 3};
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_INVALID);
    desc.panelBounds = (CompositorBounds) {INT32_MAX, 2, 1, 1};
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_LIMIT);
    desc.panelBounds = (CompositorBounds) {2, 2, 3, 3};
    desc.foregroundCount = 1;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_INVALID);
    const CompositorSurface *sources[] = {prior};
    desc.foreground = sources;
    desc.foregroundCount = COMPOSITOR_MAX_SOURCES + 1;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_LIMIT);
    desc.foregroundCount = 0;
    desc.foreground = nullptr;
    FilterToken unsupported = UINT64_C(0x8000000000000000);
    desc.foregroundFilters = &unsupported;
    desc.foregroundFilterCount = 1;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_UNSUPPORTED);
    desc.foregroundFilterCount = 0;
    desc.backdropFilters = &unsupported;
    desc.backdropFilterCount = 1;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_UNSUPPORTED);
    FilterToken blur = Filter_scatterBlur(3);
    desc.backdropFilters = &blur;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_LIMIT);
    desc.backdropFilterCount = 0;
    desc.elementFilters = &unsupported;
    desc.elementFilterCount = 1;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_UNSUPPORTED);
    desc.elementFilters = nullptr;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_INVALID);
    desc.elementFilterCount = 0;
    f32 *p = CompositorSurface_pixels(prior);
    p[3] = .5f;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_UNSUPPORTED);
    p[3] = 1;
    p[0] = NAN;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_INVALID);
    p[0] = .25f;
    assert(out == prior);
    CompositorSurface *recovered = nullptr;
    assert(Compositor_scopedScene(&desc, &recovered) == COMPOSITOR_OK);
    CompositorSurface_destroy(recovered);
    /* Failed private replacement must not change public scene/output. */
    p[0] = FLT_MAX;
    FilterToken f64Gain = Filter_gain(2);
    desc.backdropFilters = &f64Gain;
    desc.backdropFilterCount = 1;
    /* Move huge pixel into actual backdrop source region. */
    p[4 * (2 * 7 + 2)] = FLT_MAX;
    assert(Compositor_scopedScene(&desc, &out) == COMPOSITOR_LIMIT);
    assert(out == prior && p[4 * (2 * 7 + 2)] == FLT_MAX);
    CompositorSurface_destroy(prior);
}

// Runs crop, backdrop, foreground, spill, and rejection scope contracts.
int main() {
    cropContract();
    backdropReplacement();
    foregroundClipAndElementSpill();
    noBackdropDoesNotBakePrior();
    rejections();
    puts("compositor three-scope CPU replacement/clip/spill contracts: PASS");
    return 0;
}
