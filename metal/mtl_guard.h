#ifndef GRAPHVEX_MTL_GUARD_H
#define GRAPHVEX_MTL_GUARD_H

/** metal/mtl_guard.h, header-only deterministic Metal seam check. */

#include "vex.h"

#ifdef NDEBUG
#define MtlGuard_check(seam, device, queue, deviceLost) ((discard) 0)
#else
static inline bool MtlGuard_checkImpl(string seam, const_any device, const_any queue, bool *deviceLost) {
    if (deviceLost && *deviceLost)
        return false;
    if (!seam || !device || !queue) {
        THROW("MtlGuard: invalid seam (%s) device=%p queue=%p", seam ? seam : "null", device, queue);
        return false;
    }
    return true;
}
#define MtlGuard_check(seam, device, queue, deviceLost) MtlGuard_checkImpl((seam), (device), (queue), (deviceLost))
#endif

#endif
