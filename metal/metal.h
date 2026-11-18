#ifndef GRAPHVEX_METAL_H
#define GRAPHVEX_METAL_H

// metal/metal.h — internal Metal driver door (Apple targets only).
// Driver Axis Seam Law: fails with #error on non-Apple targets.

#include "core/core.h"

#if !defined(VEX_PLATFORM_APPLE)
#error "graphvex/metal driver requires VEX_PLATFORM_APPLE"
#endif

#include "metal/mtl_guard.h"

#endif
