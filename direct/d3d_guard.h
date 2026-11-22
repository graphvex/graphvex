#ifndef GRAPHVEX_D3D_GUARD_H
#define GRAPHVEX_D3D_GUARD_H

/** direct/d3d_guard.h, header-only deterministic Direct3D 12 seam check. */

#include "vex.h"

#ifdef NDEBUG
#define D3dGuard_check(seam, device, queue, deviceLost) ((discard) 0)
#else
static inline bool D3dGuard_checkImpl(const utf8 *seam, const_any device, const_any queue, bool *deviceLost) {
    if (deviceLost && *deviceLost)
        return false;
    if (!seam || !device || !queue) {
        THROW("D3dGuard: invalid seam (%s) device=%p queue=%p", seam ? seam : "null", device, queue);
        return false;
    }
    return true;
}
#define D3dGuard_check(seam, device, queue, deviceLost) D3dGuard_checkImpl((seam), (device), (queue), (deviceLost))
#endif

#endif
