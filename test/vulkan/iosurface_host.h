#ifndef IOSURFACE_HOST_H
#define IOSURFACE_HOST_H

#include <stddef.h>
#include <stdint.h>

// graphvex/test/vulkan/iosurface_host.h
//
// A tiny, graphvex-free helper: the platform (R1) side of the zero-copy seam.
// It creates and reads a real IOSurface with plain C types — no Apple header
// leaks here, so a test can include graphvex alongside this without the Carbon
// `Rect` colliding with graphvex's `Rect`. In the real stack this role is
// hotcwap's (window_cocoa.m), owned by R1.

void *IosHost_create(int width, int height);   // an IOSurfaceRef, or NULL
void  IosHost_release(void *surface);

// Lock for read and return the base address; stride out is Bytes/row.
// Returns NULL if the lock failed.
const uint8_t *IosHost_lockRead(void *surface, size_t *outStride);
void IosHost_unlock(void *surface);

#endif // IOSURFACE_HOST_H
