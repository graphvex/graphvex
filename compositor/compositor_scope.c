#include "vex.h"
#include "compositor/compositor_scope.h"

#include <string.h>

;;DEFINITION
/* Explicit CPU scope scheduler: backdrop reads only a borrowed painter-prefix;
 * foreground excludes decoration and is filtered before panel clipping; whole
 * element filters run after assembly and may spill outside the panel. Owned
 * private intermediates make every failure atomic at the public out seam.
 * An opaque prior-scene contract permits exact coverage replacement without
 * compositing a complete scene twice. No opaque backend handles or UI classes. */
;;OVERVIEW
/* No retained class fields. CompositorScopeDesc is a borrowed cold submission
 * record described by compositor_scope.h. Public: crop/scopedScene. Private:
 * contains, sceneCheck, replacementCopy. Integer world origins survive crops,
 * group assembly and final viewport clipping. Fixed scatter support is symmetric
 * so filterBounds determines the backdrop snapshot's cumulative sample halo.
 * Rectangle-only binary masks; nonzero radii/translucent baseline unsupported. */

// Reports whether the inner world-space rectangle is fully contained in outer.
static bool contains(CompositorBounds outer, CompositorBounds inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
        (i64) inner.x + inner.width <= (i64) outer.x + outer.width &&
        (i64) inner.y + inner.height <= (i64) outer.y + outer.height;
}

// Copies the source/bounds intersection into an owned surface with the requested origin and extent.
CompositorStatus Compositor_crop(const CompositorSurface *source,
                                 CompositorBounds bounds, CompositorSurface **out) {
    if (!out)
        return COMPOSITOR_INVALID;
    CompositorStatus status = CompositorSurface_validate(source);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorSurface *crop = nullptr;
    status = CompositorSurface_create(bounds, &crop);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorBounds s = CompositorSurface_bounds(source);
    i64 x0 = s.x > bounds.x ? s.x : bounds.x;
    i64 y0 = s.y > bounds.y ? s.y : bounds.y;
    i64 sx1 = (i64) s.x + s.width, dx1 = (i64) bounds.x + bounds.width;
    i64 sy1 = (i64) s.y + s.height, dy1 = (i64) bounds.y + bounds.height;
    i64 x1 = sx1 < dx1 ? sx1 : dx1, y1 = sy1 < dy1 ? sy1 : dy1;
    const f32 *sp = CompositorSurface_constPixels(source);
    f32 *dp = CompositorSurface_pixels(crop);
    if (x1 > x0) {
        bytes Bytes = (bytes) (x1 - x0) * 4 * sizeof(f32);
        for (i64 y = y0; y < y1; ++y) {
            const f32 *row = sp + 4 * ((bytes) (y - s.y) * s.width + (bytes) (x0 - s.x));
            f32 *dest = dp + 4 * ((bytes) (y - bounds.y) * bounds.width + (bytes) (x0 - bounds.x));
            memcpy(dest, row, Bytes);
        }
    }
    *out = crop;
    return COMPOSITOR_OK;
}

// Validates scope inputs and verifies backdrop coverage for every requested filter tap.
static CompositorStatus sceneCheck(const CompositorScopeDesc *desc) {
    if (!desc)
        return COMPOSITOR_INVALID;
    CompositorScopeDesc d = *desc;
    if (d.foregroundCount && !d.foreground)
        return COMPOSITOR_INVALID;
    if (d.radius)
        return COMPOSITOR_UNSUPPORTED;
    if (d.foregroundCount > COMPOSITOR_MAX_SOURCES)
        return COMPOSITOR_LIMIT;
    CompositorStatus status = CompositorSurface_validate(d.priorScene);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorBounds prior = CompositorSurface_bounds(d.priorScene);
    CompositorBounds panel = d.panelBounds;
    if (!prior.width || !panel.width || !panel.height)
        return COMPOSITOR_INVALID;
    const f32 *pixels = CompositorSurface_constPixels(d.priorScene);
    for (bytes i = 0; i < (bytes) prior.width * prior.height; ++i)
        if (pixels[4 * i + 3] != 1)
            return COMPOSITOR_UNSUPPORTED;
    CompositorBounds support;
    status = Compositor_filterBounds(panel, nullptr, 0, &support);
    if (status != COMPOSITOR_OK)
        return status;
    if (!contains(prior, panel))
        return COMPOSITOR_LIMIT;
    status = Compositor_filterBounds(panel, d.backdropFilters,
                                     d.backdropFilterCount, &support);
    if (status != COMPOSITOR_OK)
        return status;
    if (!contains(prior, support))
        return COMPOSITOR_LIMIT;
    /* Validate even an empty foreground's stack; apply performs source-specific
     * support/work checks after isolation. Element starts at panel bounds. */
    status = Compositor_filterBounds((CompositorBounds) {0}, d.foregroundFilters,
                                     d.foregroundFilterCount, &support);
    if (status != COMPOSITOR_OK)
        return status;
    return Compositor_filterBounds(panel, d.elementFilters,
                                    d.elementFilterCount, &support);
}

/* Destination is a private opaque prior copy. No full-scene source-over call:
 * each covered pixel is replaced using the filtered panel's alpha as coverage.
 * On this opaque baseline this also gives the correct front-content over rule.
 * Failure need not roll back a private copy, which is destroyed by the caller. */
static CompositorStatus replacementCopy(const CompositorSurface *group,
                                        CompositorSurface *scene) {
    CompositorStatus status = CompositorSurface_validate(group);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorBounds s = CompositorSurface_bounds(group), d = CompositorSurface_bounds(scene);
    i64 x0 = s.x > d.x ? s.x : d.x, y0 = s.y > d.y ? s.y : d.y;
    i64 sx1 = (i64) s.x + s.width, dx1 = (i64) d.x + d.width;
    i64 sy1 = (i64) s.y + s.height, dy1 = (i64) d.y + d.height;
    i64 x1 = sx1 < dx1 ? sx1 : dx1, y1 = sy1 < dy1 ? sy1 : dy1;
    const f32 *sp = CompositorSurface_constPixels(group);
    f32 *dp = CompositorSurface_pixels(scene);
    for (i64 y = y0; y < y1; ++y) {
        for (i64 x = x0; x < x1; ++x) {
            const f32 *p = sp + 4 * ((bytes) (y - s.y) * s.width + (bytes) (x - s.x));
            f32 *q = dp + 4 * ((bytes) (y - d.y) * d.width + (bytes) (x - d.x));
            for (u32 c = 0; c < 3; ++c) {
                d64 value = p[c] + q[c] * (1.0 - p[3]);
                if (value > FLT_MAX)
                    return COMPOSITOR_LIMIT;
                q[c] = (f32) value;
            }
            q[3] = 1;
        }
    }
    return COMPOSITOR_OK;
}

// Applies backdrop, foreground, decoration, and element scopes to a private scene copy.
CompositorStatus Compositor_scopedScene(const CompositorScopeDesc *desc,
                                        CompositorSurface **out) {
    if (!out)
        return COMPOSITOR_INVALID;
    CompositorStatus status = sceneCheck(desc);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorScopeDesc d = *desc;
    CompositorSurface *snapshot = nullptr, *backdrop = nullptr, *panel = nullptr;
    CompositorSurface *foreground = nullptr, *clippedForeground = nullptr;
    CompositorSurface *clippedDecoration = nullptr, *filteredPanel = nullptr, *scene = nullptr;
    status = CompositorSurface_create(d.panelBounds, &panel);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    if (d.backdropFilterCount) {
        CompositorBounds halo;
        status = Compositor_filterBounds(d.panelBounds, d.backdropFilters,
                                         d.backdropFilterCount, &halo);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        status = Compositor_crop(d.priorScene, halo, &snapshot);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        const CompositorSurface *sources[] = {snapshot};
        status = Compositor_compose(sources, 1, d.backdropFilters,
                                    d.backdropFilterCount, &backdrop);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        CompositorSurface_destroy(snapshot);
        snapshot = nullptr;
        status = Compositor_crop(backdrop, d.panelBounds, &snapshot);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        /* Prior snapshot is opaque and includes all taps. Fixed scatter's
         * alpha sum can round slightly below one; enforce exact coverage 1
         * in the proven fully supported crop, not edge renormalization. */
        f32 *p = CompositorSurface_pixels(snapshot);
        CompositorBounds b = d.panelBounds;
        for (bytes i = 0; i < (bytes) b.width * b.height; ++i)
            p[4 * i + 3] = 1;
        CompositorSurface_destroy(panel);
        panel = snapshot;
        snapshot = nullptr;
    }
    if (d.decoration) {
        status = Compositor_crop(d.decoration, d.panelBounds, &clippedDecoration);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        status = Compositor_sourceOver(clippedDecoration, panel);
        if (status != COMPOSITOR_OK)
            goto cleanup;
    }
    status = Compositor_compose(d.foreground, d.foregroundCount,
                                d.foregroundFilters, d.foregroundFilterCount,
                                &foreground);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    status = Compositor_crop(foreground, d.panelBounds, &clippedForeground);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    status = Compositor_sourceOver(clippedForeground, panel);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    const CompositorSurface *sources[] = {panel};
    status = Compositor_compose(sources, 1, d.elementFilters,
                                d.elementFilterCount, &filteredPanel);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    status = Compositor_crop(d.priorScene,
                             CompositorSurface_bounds(d.priorScene), &scene);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    status = replacementCopy(filteredPanel, scene);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    *out = scene;
    scene = nullptr;
cleanup:
    CompositorSurface_destroy(snapshot);
    CompositorSurface_destroy(backdrop);
    CompositorSurface_destroy(panel);
    CompositorSurface_destroy(foreground);
    CompositorSurface_destroy(clippedForeground);
    CompositorSurface_destroy(clippedDecoration);
    CompositorSurface_destroy(filteredPanel);
    CompositorSurface_destroy(scene);
    return status;
}
