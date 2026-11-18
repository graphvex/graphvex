#ifndef GRAPHVEX_DIRECT_H
#define GRAPHVEX_DIRECT_H

// direct/direct.h — internal Direct3D 12 driver door (Windows targets only).
// Driver Axis Seam Law: fails with #error on non-Windows targets.

#include "core/core.h"

#if !defined(VEX_PLATFORM_WINDOWS)
#error "graphvex/direct driver requires VEX_PLATFORM_WINDOWS"
#endif

#include "direct/d3d_guard.h"

#endif
