// graphvex/test/vulkan/iosurface_host.c
//
// The platform half of the zero-copy seam, isolated in its own translation unit
// so no Apple header (which defines the Carbon `Rect`) meets graphvex's `Rect`.
// Mirrors what hotcwap (R1) will own for real.

#include "iosurface_host.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>

/** Creates an RGBA IOSurface with Metal-aligned row storage; invalid dimensions return nullptr. */
void *IosHost_create(int width, int height) {
    if (width <= 0 || height <= 0) return nullptr;
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(nullptr, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!d) return nullptr;
    i32 w = width, h = height, bpe = 4, fmt = 'RGBA';
    i32 bpr = ((width * 4) + 63) & ~63;   // Metal needs an aligned row stride
    CFNumberRef nw = CFNumberCreate(nullptr, kCFNumberSInt32Type, &w);
    CFNumberRef nh = CFNumberCreate(nullptr, kCFNumberSInt32Type, &h);
    CFNumberRef nbpe = CFNumberCreate(nullptr, kCFNumberSInt32Type, &bpe);
    CFNumberRef nbpr = CFNumberCreate(nullptr, kCFNumberSInt32Type, &bpr);
    CFNumberRef nfmt = CFNumberCreate(nullptr, kCFNumberSInt32Type, &fmt);
    CFDictionarySetValue(d, kIOSurfaceWidth, nw);
    CFDictionarySetValue(d, kIOSurfaceHeight, nh);
    CFDictionarySetValue(d, kIOSurfaceBytesPerElement, nbpe);
    CFDictionarySetValue(d, kIOSurfaceBytesPerRow, nbpr);
    CFDictionarySetValue(d, kIOSurfacePixelFormat, nfmt);
    IOSurfaceRef s = IOSurfaceCreate(d);
    CFRelease(nw); CFRelease(nh); CFRelease(nbpe); CFRelease(nbpr); CFRelease(nfmt);
    CFRelease(d);
    return (any) s;
}

/** Releases a surface returned by IosHost_create; nullptr is ignored. */
fn IosHost_release(any surface) {
    if (surface) CFRelease((IOSurfaceRef)surface);
}

/** Locks a surface for reading and optionally returns its row stride; lock failure returns nullptr. */
const u8 *IosHost_lockRead(any surface, bytes *outStride) {
    if (!surface) return nullptr;
    if (IOSurfaceLock((IOSurfaceRef) surface, kIOSurfaceLockReadOnly, nullptr) != kIOReturnSuccess)
        return nullptr;
    if (outStride) *outStride = IOSurfaceGetBytesPerRow((IOSurfaceRef)surface);
    return (const u8*) IOSurfaceGetBaseAddress((IOSurfaceRef)surface);
}

/** Ends the read lock acquired by IosHost_lockRead; nullptr is ignored. */
fn IosHost_unlock(any surface) {
    if (surface) IOSurfaceUnlock((IOSurfaceRef) surface, kIOSurfaceLockReadOnly, nullptr);
}
