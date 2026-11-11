#ifndef GRAPHICS_VIEWPORT_H
#define GRAPHICS_VIEWPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "util/constructor.h"

#include "graphics/graphics.h"

// graphvex R3 — graphics/viewport.h
//
// The drawable window viewport. NOT a virtual canvas: a desktop UI works in
// LOGICAL POINTS at 1x and converts to NATIVE PIXELS only at the draw boundary.
// Retina: 1 point = backingScale pixels. There is deliberately NO FIT/STRETCH /
// virtual-resolution here — that is a *scene* concern (a separate SceneCanvas),
// never the UI path (the Absolute Size and Location Law).
//
// Forward Rendering & Bounded Surface Law: the viewport's scissor is the only
// bound — nothing outside it allocates or draws.

typedef struct Viewport {
    f32 width;    // framebuffer width  in native px
    f32 height;   // framebuffer height in native px
    f32 scale;    // backingScale: points → px (2.0 retina, else 1.0)
    Rect  scissor;  // visible rect in native px (usually the full framebuffer)
} Viewport;
typedef struct ViewportPoint { f32 x; f32 y; } ViewportPoint;
typedef struct ViewportSize { f32 width; f32 height; } ViewportSize;

Viewport Viewport_0(void);   // 0x0, scale 1
#define Viewport(...) CONSTRUCTOR_DISPATCH(Viewport, __VA_ARGS__)

void Viewport_resize(Viewport *v, f32 framebufferWidthPx, f32 framebufferHeightPx,
                     f32 backingScale);

// logical points (top-left, Y-down) → native px
f32 Viewport_x(const Viewport *v, f32 pointX);
f32 Viewport_y(const Viewport *v, f32 pointY);
f32 Viewport_w(const Viewport *v, f32 pointW);
f32 Viewport_h(const Viewport *v, f32 pointH);
Rect  Viewport_rect(const Viewport *v, f32 xPoints, f32 yPoints, f32 wPoints, f32 hPoints);

// native px → logical points
ViewportPoint Viewport_toPoints(const Viewport *v, f32 px, f32 py);

// the framebuffer's logical size, in points
ViewportSize Viewport_logicalSize(const Viewport *v);

bool Viewport_isEmpty(const Viewport *v);

#endif // GRAPHICS_VIEWPORT_H
