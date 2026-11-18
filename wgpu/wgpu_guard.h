#ifndef GRAPHVEX_WGPU_GUARD_H
#define GRAPHVEX_WGPU_GUARD_H

// wgpu/wgpu_guard.h — header-only deterministic WebGPU (wgpu-native) seam check.

#include "vex.h"

#ifdef NDEBUG
#define WgpuGuard_check(seam, device, queue, deviceLost) ((discard) 0)
#else
static inline bool WgpuGuard_checkImpl(const char *seam, const_any device, const_any queue, bool *deviceLost) {
    if (deviceLost && *deviceLost) return false;
    if (!seam || !device || !queue) {
        THROW("WgpuGuard: invalid seam (%s) device=%p queue=%p", seam ? seam : "null", device, queue);
        return false;
    }
    return true;
}
#define WgpuGuard_check(seam, device, queue, deviceLost) WgpuGuard_checkImpl((seam), (device), (queue), (deviceLost))
#endif

#endif
