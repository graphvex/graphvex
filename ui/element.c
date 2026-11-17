#include "vex.h"
#include "ui/element.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "nio/property_pool.h"
#include "lang/image.h"

;;DEFINITION
/**
 * The R3 UI node owns its child tree and optionally a pooled, shareable bound.
 * Placement identity and host cursor preference stay on each node, not on that
 * shared paint bound. Cursor preference is inert numeric data: R4 interprets
 * it, while this renderer never calls an OS cursor API. Construction defaults
 * it to inheritance (-1); destroying the node destroys its preference without
 * leaving an external registration. Layout reads effective bound sizes; hit
 * traversal respects rectangular/rounded ancestor masks just like paint.
 */
;;OVERVIEW
/**
 * CLASS: Element (ui/element.c)
 * Fields in declaration order:
 *   Property *property; bool ownsProperty, dirty;
 *   f32 offsetX, offsetY; i32 anchor, pivot; const char *tag;
 *   bool pressed, visible; i32 cursorPreference; const Image *image; // borrowed
 *   struct Element *parent; struct Element **children; i32 count, cap;
 * Public API (ui/element.h):
 *   Part_point; Element_0/1, destroy; property, setProperty, ownProperty;
 *   markDirty, revalidate, isDirty; add, addAt, remove, count, child, parent,
 *   root, find, hit; eventBound, absoluteBound, resolve, bounds, paint;
 *   setSize, setMinimumSize, setMaximumSize, setOffset, setAnchor, setPivot,
 *   setTag, setRadius, setBackground, setBorder, setShadow, setShadowColor,
 *   setBlur, setClip, setPressed, setVisible;
 *   width, height, radius, anchor, pivot, isValid, isVisible, isPressed, tag;
 *   Element_setCursorPreference, Element_cursorPreference; setImage, image.
 * Private helpers: child_insert (reparent/insert), hit_rec (masked traversal).
 */

struct Element {
    Property *property;      // owned (from the default pool) or borrowed
    bool ownsProperty;
    bool dirty;

    f32 offsetX, offsetY;
    i32 anchor, pivot;
    const char *tag;
    bool pressed;
    bool visible;
    i32 cursorPreference; // per-node host preference; -1 inherits (not pooled)
    const Image *image;    // borrowed, survives through recorded DisplayLists

    struct Element *parent;
    struct Element **children;
    i32 count, cap;
};

;;GETTER
static inline bool Element_ownsProperty(const Element *self) { return (*self).ownsProperty; }
;;SETTER
static inline fn Element_setOwnsProperty(Element *self, bool v) { (*self).ownsProperty = v; }
;;GETTER
static inline bool Element_dirty(const Element *self) { return (*self).dirty; }
;;SETTER
static inline fn Element_setDirty(Element *self, bool v) { (*self).dirty = v; }
;;GETTER
static inline f32 Element_offsetX(const Element *self) { return (*self).offsetX; }
;;SETTER
static inline fn Element_setOffsetX(Element *self, f32 v) { (*self).offsetX = v; }
;;GETTER
static inline f32 Element_offsetY(const Element *self) { return (*self).offsetY; }
;;SETTER
static inline fn Element_setOffsetY(Element *self, f32 v) { (*self).offsetY = v; }
;;SETTER
static inline fn Element_setParent(Element *self, Element *v) { (*self).parent = v; }
;;GETTER
static inline Element **Element_children(const Element *self) { return (*self).children; }
;;SETTER
static inline fn Element_setChildren(Element *self, Element **v) { (*self).children = v; }
;;SETTER
static inline fn Element_setCount(Element *self, i32 v) { (*self).count = v; }
;;GETTER
static inline i32 Element_cap(const Element *self) { return (*self).cap; }
;;SETTER
static inline fn Element_setCap(Element *self, i32 v) { (*self).cap = v; }
;;SETTER
static inline fn Element_setPropertyPtr(Element *self, Property *v) { (*self).property = v; }
;;SETTER
static inline fn Element_setAnchorField(Element *self, i32 v) { (*self).anchor = v; }
;;SETTER
static inline fn Element_setPivotField(Element *self, i32 v) { (*self).pivot = v; }
;;SETTER
static inline fn Element_setTagField(Element *self, const char *v) { (*self).tag = v; }
;;SETTER
static inline fn Element_setPressedField(Element *self, bool v) { (*self).pressed = v; }
;;SETTER
static inline fn Element_setVisibleField(Element *self, bool v) { (*self).visible = v; }
;;SETTER
static inline fn Element_setCursorPreferenceField(Element *self, i32 v) { (*self).cursorPreference = v; }
;;SETTER
static inline fn Element_setImageField(Element *self, const Image *v) { (*self).image = v; }

;;SETTER
static inline fn Property_setW(Property *self, f32 v) { (*self).w = v; }
;;SETTER
static inline fn Property_setH(Property *self, f32 v) { (*self).h = v; }
;;SETTER
static inline fn Property_setRadius(Property *self, f32 v) { (*self).radius = v; }
;;SETTER
static inline fn Property_setBackground(Property *self, Color v) { (*self).background = v; }
;;SETTER
static inline fn Property_setBorder(Property *self, Color v) { (*self).border = v; }
;;SETTER
static inline fn Property_setBorderWidth(Property *self, f32 v) { (*self).borderWidth = v; }
;;SETTER
static inline fn Property_setShadow(Property *self, Color v) { (*self).shadow = v; }
;;SETTER
static inline fn Property_setShadowX(Property *self, f32 v) { (*self).shadowX = v; }
;;SETTER
static inline fn Property_setShadowY(Property *self, f32 v) { (*self).shadowY = v; }
;;SETTER
static inline fn Property_setShadowBlur(Property *self, f32 v) { (*self).shadowBlur = v; }
;;SETTER
static inline fn Property_setBlur(Property *self, f32 v) { (*self).blur = v; }
;;SETTER
static inline fn Property_setClip(Property *self, bool v) { (*self).clip = v; }

// ── construction ────────────────────────────────────────────────────────────
// Resolves one of the nine anchor/pivot positions within a rectangle; invalid parts select top-left.
Point Part_point(Rect r, i32 part) {
    if (part < 0 || part >= PART_COUNT) part = PART_TOP_LEFT;
    i32 col = part % 3;   // 0 left, 1 centre, 2 right
    i32 row = part / 3;   // 0 top,  1 middle, 2 bottom
    return (Point){r.x + (f32)col * 0.5f * r.w, r.y + (f32)row * 0.5f * r.h};
}

// Creates an element with a pooled property record initialized from d or the default style.
Element *Element_1(const ElementDesc *d) {
    Element *e = calloc(1, sizeof *e);
    if (!e) return nullptr;
    Property *p = PropertyPool_alloc(PropertyPool_default(), nullptr);
    if (!p) { free(e); return nullptr; }
    Element_setPropertyPtr(e, p);
    Element_setOwnsProperty(e, true);
    if (d) {
        ElementDesc desc = *d;
        Property_setW(p, desc.width);
        Property_setH(p, desc.height);
        Property_setRadius(p, desc.radius);
        Property_setBackground(p, desc.background);
        Property_setBorder(p, desc.border);
        Property_setBorderWidth(p, desc.borderWidth);
        Property_setShadow(p, desc.shadow);
        Property_setShadowX(p, desc.shadowX);
        Property_setShadowY(p, desc.shadowY);
        Property_setShadowBlur(p, desc.shadowBlur);
        Property_setBlur(p, desc.blur);
        Element_setOffsetX(e, desc.offsetX);
        Element_setOffsetY(e, desc.offsetY);
        Element_setAnchorField(e, desc.anchor);
        Element_setPivotField(e, desc.pivot);
        Element_setTagField(e, desc.tag);
    } else {
        Property_setBackground(p, COLOR_WHITE);
        Property_setBorder(p, COLOR_CLEAR);
        Property_setShadow(p, COLOR_CLEAR);
        Element_setAnchorField(e, PART_TOP_LEFT);
        Element_setPivotField(e, PART_TOP_LEFT);
    }
    Element_setVisibleField(e, true);
    Element_setCursorPreferenceField(e, -1);
    return e;
}

// Creates a visible default element with zero extent and inherited cursor preference.
Element *Element_0() { return Element_1(nullptr); }

// Recursively destroys the owned child tree and releases only an owned property record.
fn Element_destroy(Element *e) {
    if (!e) return;
    Element **children = Element_children(e);
    i32 count = Element_count(e);
    for (i32 i = 0; i < count; i++) Element_destroy(children[i]);
    free(children);
    if (Element_ownsProperty(e) && Element_property(e))
        PropertyPool_release(PropertyPool_default(), Element_property(e));
    free(e);
}

// ── the bound ───────────────────────────────────────────────────────────────
// Returns the element's property record as a borrowed pointer.
Property *Element_property(const Element *e) { return e ? (*e).property : nullptr; }

// Binds a borrowed property record, releasing any previously owned record and marking layout dirty.
;;SETTER
Element *Element_setProperty(Element *e, Property *property) {
    if (!e) return e;
    if (Element_ownsProperty(e) && Element_property(e))
        PropertyPool_release(PropertyPool_default(), Element_property(e));
    (*e).property = property;
    Element_setOwnsProperty(e, false);
    Element_markDirty(e);
    return e;
}

// Copies the current property into an element-owned pooled record.
Element *Element_ownProperty(Element *e) {
    if (!e) return e;
    Property *copy = PropertyPool_alloc(PropertyPool_default(), Element_property(e));
    if (!copy) return e;
    if (Element_ownsProperty(e) && Element_property(e))
        PropertyPool_release(PropertyPool_default(), Element_property(e));
    Element_setPropertyPtr(e, copy);
    Element_setOwnsProperty(e, true);
    Element_markDirty(e);
    return e;
}

// ── revalidation ────────────────────────────────────────────────────────────
// Marks this element and its ancestors dirty so revalidation reaches the changed branch.
fn Element_markDirty(Element *e) {
    for (Element *n = e; n; n = Element_parent(n)) {
        if (Element_dirty(n)) break;
        Element_setDirty(n, true);
    }
}

// Clears dirty state bottom-up while pruning already-clean subtrees.
fn Element_revalidate(Element *root) {
    if (!root || !Element_dirty(root)) return;    // prune clean subtrees
    Element **children = Element_children(root);
    i32 count = Element_count(root);
    for (i32 i = 0; i < count; i++) {
        Element *c = children[i];
        if (Element_dirty(c)) Element_revalidate(c);
    }
    Element_setDirty(root, false);
}

// Reports whether this element is marked for revalidation.
bool Element_isDirty(const Element *e) { return e && Element_dirty(e); }

// ── tree ────────────────────────────────────────────────────────────────────
// Inserts child at a clamped position and updates its parent link and dirty state.
static fn child_insert(Element *parent, Element *child, i32 index) {
    if (Element_count(parent) == Element_cap(parent)) {
        i32 nextCap = Element_cap(parent) ? Element_cap(parent) * 2 : 8;
        Element_setCap(parent, nextCap);
        Element **grown = realloc(Element_children(parent), (bytes)(nextCap) * sizeof *grown);
        if (!grown) return;
        Element_setChildren(parent, grown);
    }
    i32 count = Element_count(parent);
    Element **children = Element_children(parent);
    if (index < 0 || index > count) index = count;
    memmove(&children[index + 1], &children[index],
            (bytes)(count - index) * sizeof *children);
    children[index] = child;
    Element_setCount(parent, count + 1);
    Element_setParent(child, parent);
    Element_markDirty(parent);
}

// Appends child to parent and returns child; null inputs leave the tree unchanged.
Element *Element_add(Element *parent, Element *child) {
    if (!parent || !child) return child;
    child_insert(parent, child, Element_count(parent));
    return child;
}

// Inserts child at index (out-of-range indices append) and returns child.
Element *Element_addAt(Element *parent, Element *child, i32 index) {
    if (!parent || !child) return child;
    child_insert(parent, child, index);
    return child;
}

// Detaches child from its parent without destroying it; returns false when unattached.
bool Element_remove(Element *child) {
    if (!child || !Element_parent(child)) return false;
    Element *p = Element_parent(child);
    Element **children = Element_children(p);
    i32 count = Element_count(p);
    for (i32 i = 0; i < count; i++) {
        if (children[i] != child) continue;
        memmove(&children[i], &children[i + 1], (bytes)(count - i - 1) * sizeof *children);
        Element_setCount(p, count - 1);
        Element_setParent(child, nullptr);
        Element_markDirty(p);
        return true;
    }
    return false;
}

// Returns the number of direct children, or zero for null.
i32 Element_count(const Element *e) { return e ? (*e).count : 0; }

// Returns a borrowed child at index, or nullptr when the index is invalid.
Element *Element_child(const Element *e, i32 index) {
    if (!e || index < 0 || index >= Element_count(e)) return nullptr;
    return Element_children(e)[index];
}

// Returns the borrowed parent pointer, or nullptr for a root or null input.
Element *Element_parent(const Element *e) { return e ? (*e).parent : nullptr; }

// Walks parent links to return the root of the element's current tree.
Element *Element_root(Element *e) {
    if (!e) return nullptr;
    while (Element_parent(e)) e = Element_parent(e);
    return e;
}

// Searches depth-first for the first element whose non-null tag matches tag.
Element *Element_find(Element *root, const char *tag) {
    if (!root || !tag) return nullptr;
    if (Element_tag(root) && !strcmp(Element_tag(root), tag)) return root;
    Element **children = Element_children(root);
    i32 count = Element_count(root);
    for (i32 i = 0; i < count; i++) {
        Element *hit = Element_find(children[i], tag);
        if (hit) return hit;
    }
    return nullptr;
}

// ── hit-test (deepest top-most element under the point) ─────────────────────
// Tests visible descendants in reverse paint order while honoring ancestor clip shapes.
static Element *hit_rec(Element *e, Rect absolute, f32 x, f32 y) {
    if (!e || !Element_isVisible(e) || !Element_property(e)) return nullptr;
    Property prop = *Element_property(e);
    // Descendants cannot receive input where an ancestor masks their paint.
    if (prop.clip || prop.radius > 0.0f) {
        if (!Rect_contains(absolute, x, y)) return nullptr;
        f32 radius = fminf(prop.radius, fminf(absolute.w, absolute.h) * 0.5f);
        if (radius > 0.0f) {
            f32 qx = fmaxf(fabsf(x - absolute.x - absolute.w * 0.5f) - (absolute.w * 0.5f - radius), 0.0f);
            f32 qy = fmaxf(fabsf(y - absolute.y - absolute.h * 0.5f) - (absolute.h * 0.5f - radius), 0.0f);
            if (qx * qx + qy * qy > radius * radius) return nullptr;
        }
    }
    Element **children = Element_children(e);
    for (i32 i = Element_count(e) - 1; i >= 0; i--) {
        Element *c = children[i];
        Rect cr = Element_resolve(c, absolute);
        Element *hit = hit_rec(c, cr, x, y);
        if (hit) return hit;
    }
    if (x >= absolute.x && x < absolute.x + absolute.w &&
        y >= absolute.y && y < absolute.y + absolute.h)
        return e;
    return nullptr;
}

// Returns the deepest topmost element hit by the point, or nullptr when none qualifies.
Element *Element_hit(Element *root, f32 x, f32 y) {
    if (!root) return nullptr;
    Rect self = {0, 0, Property_width(Element_property(root)), Property_height(Element_property(root))};
    return hit_rec(root, self, x, y);
}

// ── geometry ────────────────────────────────────────────────────────────────
// Resolves the layout/hit rectangle from parent anchor, element offset, pivot, and clamped size.
Rect Element_eventBound(const Element *e, Rect parent) {
    if (!e || !Element_property(e)) return (Rect){0, 0, 0, 0};
    f32 w = Property_width(Element_property(e));
    f32 h = Property_height(Element_property(e));
    Point anchor = Part_point(parent, Element_anchor(e));
    Rect self = {0.0f, 0.0f, w, h};
    Point pivot = Part_point(self, Element_pivot(e));
    return (Rect){anchor.x + Element_offsetX(e) - pivot.x,
                  anchor.y + Element_offsetY(e) - pivot.y,
                  w, h};
}

// Resolves the element's event bound; kept as the layout-facing alias.
Rect Element_resolve(const Element *e, Rect parent) {
    return Element_eventBound(e, parent);
}

// Sets stored dimensions on the bound and marks the element tree dirty.
Element *Element_setSize(Element *e, f32 w, f32 h) {
    Property *property = Element_property(e);
    if (property) { Property_setW(property, w); Property_setH(property, h); Element_markDirty(e); }
    return e;
}
// Sets nonnegative minimum dimensions on the shared bound and marks it dirty.
Element *Element_setMinimumSize(Element *e, f32 w, f32 h) {
    if (e && Element_property(e)) { Property_setMinSize(Element_property(e), w, h); Element_markDirty(e); }
    return e;
}
// Sets nonnegative maximum dimensions on the shared bound and marks it dirty.
Element *Element_setMaximumSize(Element *e, f32 w, f32 h) {
    if (e && Element_property(e)) { Property_setMaxSize(Element_property(e), w, h); Element_markDirty(e); }
    return e;
}
// Sets the element's anchor-relative offset and marks its ancestors dirty.
Element *Element_setOffset(Element *e, f32 x, f32 y) {
    if (e) { Element_setOffsetX(e, x); Element_setOffsetY(e, y); Element_markDirty(e); }
    return e;
}
// Sets a valid parent anchor part; invalid part values are ignored.
;;SETTER
Element *Element_setAnchor(Element *e, i32 anchor) {
    if (e && anchor >= 0 && anchor < PART_COUNT) { (*e).anchor = anchor; Element_markDirty(e); }
    return e;
}
// Sets a valid self pivot part; invalid part values are ignored.
;;SETTER
Element *Element_setPivot(Element *e, i32 pivot) {
    if (e && pivot >= 0 && pivot < PART_COUNT) { (*e).pivot = pivot; Element_markDirty(e); }
    return e;
}
// Stores a borrowed tag pointer used by Element_find.
;;SETTER
Element *Element_setTag(Element *e, const char *tag) {
    if (e) (*e).tag = tag;
    return e;
}

// ── visual ──────────────────────────────────────────────────────────────────
// Sets the corner radius, clamping negative values to zero, and marks the tree dirty.
Element *Element_setRadius(Element *e, f32 radius) {
    Property *property = Element_property(e);
    if (property) { Property_setRadius(property, radius < 0.0f ? 0.0f : radius); Element_markDirty(e); }
    return e;
}
// Sets the bound's background color and marks the tree dirty.
Element *Element_setBackground(Element *e, Color color) {
    Property *property = Element_property(e);
    if (property) { Property_setBackground(property, color); Element_markDirty(e); }
    return e;
}
// Sets border color and nonnegative width on the bound.
Element *Element_setBorder(Element *e, Color color, f32 width) {
    Property *property = Element_property(e);
    if (property) {
        Property_setBorder(property, color);
        Property_setBorderWidth(property, width < 0.0f ? 0.0f : width);
        Element_markDirty(e);
    }
    return e;
}
// Sets shadow offset and nonnegative blur, assigning a default translucent black if unset.
Element *Element_setShadow(Element *e, f32 offsetX, f32 offsetY, f32 blur) {
    Property *property = Element_property(e);
    if (!property) return e;
    Property_setShadowX(property, offsetX);
    Property_setShadowY(property, offsetY);
    Property_setShadowBlur(property, blur < 0.0f ? 0.0f : blur);
    // a lone setShadow() should *do* something: default the colour if unset
    Property prop = *property;
    if (Color_alpha(prop.shadow) == 0u)
        Property_setShadow(property, COLOR_RGBA(0, 0, 0, 128));
    Element_markDirty(e);
    return e;
}
// Sets the shadow color on the bound and marks the tree dirty.
Element *Element_setShadowColor(Element *e, Color color) {
    Property *property = Element_property(e);
    if (property) { Property_setShadow(property, color); Element_markDirty(e); }
    return e;
}
// Sets nonnegative element-edge blur on the bound and marks the tree dirty.
Element *Element_setBlur(Element *e, f32 blur) {
    Property *property = Element_property(e);
    if (property) { Property_setBlur(property, blur < 0.0f ? 0.0f : blur); Element_markDirty(e); }
    return e;
}
// Enables or disables clipping of descendants to this element's bound.
Element *Element_setClip(Element *e, bool clip) {
    Property *property = Element_property(e);
    if (property) { Property_setClip(property, clip); Element_markDirty(e); }
    return e;
}
// Updates the pressed visual state without changing layout.
;;SETTER
Element *Element_setPressed(Element *e, bool pressed) {
    if (e) (*e).pressed = pressed;
    return e;
}
// Sets whether this element and its subtree participate in painting and hit testing.
;;SETTER
Element *Element_setVisible(Element *e, bool visible) {
    if (e) (*e).visible = visible;
    return e;
}

// Stores a borrowed image for element painting and marks the element tree dirty.
;;SETTER
Element *Element_setImage(Element *e, const Image *image) {
    if (e) {
        (*e).image = image;
        Element_markDirty(e);
    }
    return e;
}

// Returns the element's borrowed image pointer.
const Image *Element_image(const Element *e) {
    return e ? (*e).image : nullptr;
}

// ── queries ─────────────────────────────────────────────────────────────────
// Stores the host-interpreted cursor preference; -1 denotes inherited preference.
;;SETTER
Element *Element_setCursorPreference(Element *e, i32 cursor) {
    if (e) (*e).cursorPreference = cursor;
    return e;
}
// Returns the stored cursor preference, or -1 when no element is supplied.
i32 Element_cursorPreference(const Element *e) { return e ? (*e).cursorPreference : -1; }
// Returns the effective clamped property width, or zero without a bound.
f32 Element_width(const Element *e) { return Element_property(e) ? Property_width(Element_property(e)) : 0.0f; }
// Returns the effective clamped property height, or zero without a bound.
f32 Element_height(const Element *e) { return Element_property(e) ? Property_height(Element_property(e)) : 0.0f; }
// Returns the bound's corner radius, or zero when unavailable.
f32 Element_radius(const Element *e) {
    const Property *property = Element_property(e);
    return property ? (*property).radius : 0.0f;
}
// Returns the parent anchor part, defaulting to top-left for null.
i32   Element_anchor(const Element *e) { return e ? (*e).anchor : PART_TOP_LEFT; }
// Returns the self pivot part, defaulting to top-left for null.
i32   Element_pivot(const Element *e) { return e ? (*e).pivot : PART_TOP_LEFT; }
// Reports whether a bound exists and its stored width and height are nonnegative.
bool  Element_isValid(const Element *e) {
    const Property *property = Element_property(e);
    if (!property) return false;
    Property prop = *property;
    return prop.w >= 0.0f && prop.h >= 0.0f;
}
// Reports whether the element is visible.
;;GETTER
bool  Element_isVisible(const Element *e) { return e && (*e).visible; }
// Reports whether the element is in its pressed state.
;;GETTER
bool  Element_isPressed(const Element *e) { return e && (*e).pressed; }
// Returns the borrowed tag string, or nullptr for a null element.
const char *Element_tag(const Element *e) { return e ? (*e).tag : nullptr; }

// ── paint ───────────────────────────────────────────────────────────────────
// Records this element's shadow, body, image, and children into the supplied display list.
fn Element_paint(const Element *e, Rect absolute, DisplayList *dl) {
    if (!e || !dl || !Element_isVisible(e) || !Element_property(e)) return;
    Property p = *Element_property(e);

    f32 eb = p.blur > 0.0f ? p.blur : 0.0f;   // the element's own soft edge
    bool paints = Color_alpha(p.background) != 0u ||
                  (Color_alpha(p.border) != 0u && p.borderWidth > 0.0f) ||
                  Color_alpha(p.shadow) != 0u;
    if (paints) {
        if (Color_alpha(p.shadow) != 0u) {
            // shadow = the shape, offset, with a blur margin to fit the falloff
            f32 sb = p.shadowBlur > 0.0f ? p.shadowBlur : 0.0f;
            Rect sr = {absolute.x + p.shadowX - sb, absolute.y + p.shadowY - sb,
                       absolute.w + 2.0f * sb, absolute.h + 2.0f * sb};
            Brush shadow = {p.shadow, p.radius, 0u, 0.0f, sb};
            DisplayList_rect(dl, sr, &shadow);
        }
        // the body grows by the blur margin too; the layout rect never moves
        Rect body = {absolute.x - eb, absolute.y - eb,
                     absolute.w + 2.0f * eb, absolute.h + 2.0f * eb};
        Brush b = {p.background, p.radius, p.border, p.borderWidth, eb};
        DisplayList_rect(dl, body, &b);
        if (Element_isPressed(e)) {
            Brush dim = {COLOR_RGBA(0, 0, 0, 48), p.radius, 0u, 0.0f, eb};
            DisplayList_rect(dl, body, &dim);
        }
    }

    // Image content uses the ordinary paint path, not a widget-specific backend.
    // Its own rounded mask is balanced before descendants, even when clip=false.
    const Image *image = Element_image(e);
    if (image && Image_isValid(image) && !Rect_isEmpty(absolute)) {
        bool rounded = p.radius > 0.0f;
        if (rounded)
            DisplayList_clipRounded(dl, absolute, p.radius);
        Rect source = {0, 0, (f32) Image_width(image), (f32) Image_height(image)};
        DisplayList_image(dl, image, source, absolute);
        if (rounded)
            DisplayList_unclip(dl);
    }

    // A clipping / rounded parent MASKS its children: the radius means what it
    // says, and a scroll viewport clips to its rect.
    Element **children = Element_children(e);
    i32 count = Element_count(e);
    bool clips = (p.clip || p.radius > 0.0f) && count > 0;
    if (clips) DisplayList_clipRounded(dl, absolute, p.radius);
    for (i32 i = 0; i < count; i++) {
        Element *c = children[i];
        Element_paint(c, Element_resolve(c, absolute), dl);
    }
    if (clips) DisplayList_unclip(dl);
}

// Computes a conservative world-space paint bound including own effects and clipped descendants.
Rect Element_absoluteBound(const Element *e, Rect parent) {
    if (!e || !Element_isVisible(e) || !Element_property(e)) return (Rect){0, 0, 0, 0};
    Property property = *Element_property(e);
    Rect base = Element_eventBound(e, parent);
    // Current styles affect the node's own shape, not the assembled subtree.
    // Ordered group-filter support will be integrated at the compositor seam.
    f32 eb = property.blur > 0.0f ? property.blur : 0.0f;
    f32 x0 = base.x - eb, y0 = base.y - eb;
    f32 x1 = base.x + base.w + eb, y1 = base.y + base.h + eb;
    if (Color_alpha(property.shadow) != 0u) {
        f32 sb = property.shadowBlur > 0.0f ? property.shadowBlur : 0.0f;
        f32 sx0 = base.x + property.shadowX - sb, sy0 = base.y + property.shadowY - sb;
        f32 sx1 = base.x + base.w + property.shadowX + sb, sy1 = base.y + base.h + property.shadowY + sb;
        if (sx0 < x0) x0 = sx0;
        if (sy0 < y0) y0 = sy0;
        if (sx1 > x1) x1 = sx1;
        if (sy1 > y1) y1 = sy1;
    }
    Rect result = {x0, y0, x1 - x0, y1 - y0};
    bool clips = property.clip || property.radius > 0.0f;
    Element **children = Element_children(e);
    i32 count = Element_count(e);
    for (i32 i = 0; i < count; i++) {
        Rect child = Element_absoluteBound(children[i], base);
        if (clips) child = Rect_intersect(child, base);
        if (Rect_isEmpty(child)) continue;
        if (Rect_isEmpty(result)) { result = child; continue; }
        f32 left = fminf(result.x, child.x);
        f32 top = fminf(result.y, child.y);
        f32 right = fmaxf(result.x + result.w, child.x + child.w);
        f32 bottom = fmaxf(result.y + result.h, child.y + child.h);
        result = (Rect){left, top, right - left, bottom - top};
    }
    return result;
}

// Returns the absolute paint bound; compatibility alias for Element_absoluteBound.
Rect Element_bounds(const Element *e, Rect parent) {
    return Element_absoluteBound(e, parent);
}
