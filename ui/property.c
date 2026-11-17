#include "vex.h"
#include "ui/property.h"


;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Property (ui/property.c)
 * ============================================================================
 * A Property is the shared BOUND: inert placement + paint data, pooled in nio/
 * and aliased by pointer so several Elements can share one record. It owns
 * nothing and runs nothing; its one behavior is the size clamp — minW/minH and
 * maxW/maxH bound the stored w/h, and Property_width/Height expose the clamped
 * result. Because the clamp lives on the bound and not on the element, a
 * ceiling set after a size still takes effect and every aliasing element clamps
 * identically. max <= 0 means "no ceiling"; a default bound matches the element
 * defaults (opaque white body, no border/shadow, no clamps).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Property (ui/property.c)
 * ============================================================================
 * Inert, pool-allocated bound record. No owned state, no allocation here.
 *
 * STRUCT FIELDS (mirrors ui/property.h):
 * ----------------------------------------------------------------------------
 *   f32 x, y, w, h;      // placement rect (native px)
 *   f32 minW, minH;      // size floor (0 = none)
 *   f32 maxW, maxH;      // size ceiling (<= 0 = unbounded)
 *   f32 radius;          // corner radius (also clips children when > 0)
 *   bool  clip;            // clip children to this rect
 *   Color background, border; f32 borderWidth;
 *   Color shadow; f32 shadowX, shadowY, shadowBlur; f32 blur;
 *
 * FUNCTION REGISTRY (exported by ui/property.h):
 * ----------------------------------------------------------------------------
 * Core:
 *   - Property_default
 * Size clamp:
 *   - Property_setMinSize, Property_setMaxSize
 * Getters:
 *   - Property_width, Property_height
 * ============================================================================
 */

// Returns a zeroed bound with an opaque white background and no size clamps.
Property Property_default() {
    Property p = {0};
    p.background = COLOR_WHITE;
    return p;
}

// Clamp the stored extent into [min, max]; a non-positive max is no ceiling.
// Applies the minimum and an optional positive maximum to one stored dimension.
static f32 property_clamp(f32 value, f32 min, f32 max) {
    if (max > 0.0f && value > max) value = max;
    if (value < min) value = min;
    return value;
}

;;GETTER
static f32 Property_rawW(const Property *property) { return (*property).w; }
;;GETTER
static f32 Property_rawH(const Property *property) { return (*property).h; }
;;GETTER
static f32 Property_minW(const Property *property) { return (*property).minW; }
;;SETTER
static fn Property_setMinW(Property *property, f32 v) { (*property).minW = v; }
;;GETTER
static f32 Property_minH(const Property *property) { return (*property).minH; }
;;SETTER
static fn Property_setMinH(Property *property, f32 v) { (*property).minH = v; }
;;GETTER
static f32 Property_maxW(const Property *property) { return (*property).maxW; }
;;SETTER
static fn Property_setMaxW(Property *property, f32 v) { (*property).maxW = v; }
;;GETTER
static f32 Property_maxH(const Property *property) { return (*property).maxH; }
;;SETTER
static fn Property_setMaxH(Property *property, f32 v) { (*property).maxH = v; }

// Sets nonnegative minimum dimensions; zero disables each minimum.
fn Property_setMinSize(Property *property, f32 width, f32 height) {
    if (!property) return;
    Property_setMinW(property, width > 0.0f ? width : 0.0f);
    Property_setMinH(property, height > 0.0f ? height : 0.0f);
}

// Sets nonnegative maximum dimensions; zero means no upper bound.
fn Property_setMaxSize(Property *property, f32 width, f32 height) {
    if (!property) return;
    Property_setMaxW(property, width > 0.0f ? width : 0.0f);
    Property_setMaxH(property, height > 0.0f ? height : 0.0f);
}

// Returns the stored width clamped to configured limits, or zero for null.
f32 Property_width(const Property *property) {
    return property ? property_clamp(Property_rawW(property), Property_minW(property), Property_maxW(property)) : 0.0f;
}

// Returns the stored height clamped to configured limits, or zero for null.
f32 Property_height(const Property *property) {
    return property ? property_clamp(Property_rawH(property), Property_minH(property), Property_maxH(property)) : 0.0f;
}
