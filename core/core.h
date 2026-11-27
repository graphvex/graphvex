#ifndef GRAPHVEX_CORE_H
#define GRAPHVEX_CORE_H

/**
 * core/core.h -- directory door for the unified, backend-agnostic Graphics API.
 *
 * Zero Vulkan, Metal, Direct3D, or wgpu types leak here. No Swapchain.
 * Bindless image slots are always-on by construction.
 */

#include "core/pixel.h"
#include "core/rect.h"
#include "core/image.h"
#include "core/command.h"
#include "core/driver.h"
#include "core/graphics.h"
#include "core/surface.h"

#endif
