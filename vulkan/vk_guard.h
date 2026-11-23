#ifndef GRAPHVEX_VK_GUARD_H
#define GRAPHVEX_VK_GUARD_H

/**
 * vulkan/vk_guard.h, header-only deterministic Vulkan seam check.
 *
 * Ecosystem Vulkan Safety Nets Law: under NDEBUG the guard is a macro no-op
 * (zero calls, zero branches, unevaluated arguments).
 */

#include "vex.h"

#ifdef NDEBUG
#define VkGuard_check(seam, device, queue, deviceLost) ((discard) 0)
#define VkGuard_checkResource(seam, device, deviceLost) ((discard) 0)
#else
static inline bool VkGuard_checkImpl(string seam, const_any device, const_any queue, bool *deviceLost) {
    if (deviceLost && *deviceLost)
        return false;
    if (!seam || !device || !queue) {
        THROW("VkGuard: invalid seam (%s) device=%p queue=%p", seam ? seam : "null", device, queue);
        return false;
    }
    return true;
}
static inline bool VkGuard_checkResourceImpl(string seam, const_any device, bool *deviceLost) {
    if (deviceLost && *deviceLost)
        return false;
    if (!seam || !device) {
        THROW("VkGuard: invalid resource seam (%s) device=%p", seam ? seam : "null", device);
        return false;
    }
    return true;
}
#define VkGuard_check(seam, device, queue, deviceLost) VkGuard_checkImpl((seam), (device), (queue), (deviceLost))
#define VkGuard_checkResource(seam, device, deviceLost) VkGuard_checkResourceImpl((seam), (device), (deviceLost))
#endif

#endif
