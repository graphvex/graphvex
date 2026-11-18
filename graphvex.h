#ifndef GRAPHVEX_H
#define GRAPHVEX_H

// graphvex.h — the one include door of the graphvex GPU API.
//
// Tier: core (GPU Driver). Owns backend-agnostic GPU primitives and the four
// drivers (vulkan, metal, direct, wgpu). Never exposes vendor handles in public
// headers. Drawing vocabulary and UI composition belong to collage; retained
// 2D/3D scenes belong to meld.

#include "vex.h"
#include "core/core.h"
#include "shader/shader.h"

#endif
