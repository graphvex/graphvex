#ifndef GRAPHVEX_CORE_SURFACE_H
#define GRAPHVEX_CORE_SURFACE_H

/**
 * core/surface.h -- reusable double-buffered screen presentation Surface
 * inheriting GraphicsBackingImage at offset 0 (which inherits Image at offset 0).
 *
 * Surface reuses at most 2 OS backing buffers (double-buffered ceiling;
 * triple-buffering is reserved strictly for meld 3D scenes), clears and
 * repaints in place instead of destroying and rebuilding, and owns the
 * low-level Surface_present / Surface_presentImage seam invoked by Frame above.
 */

#include "core/graphics.h"

#define SURFACE_MAX_BACKING_SLOTS 2u

;;CLASSNAME("SurfaceFlag")

/**
 * Runtime state flags for a presentation Surface.
 *
 * SURFACE_FLAG_NONE      - default unattached surface
 * SURFACE_FLAG_ATTACHED  - nativeSurface is currently bound
 * SURFACE_FLAG_PURGEABLE - backing OS surfaces are marked volatile
 */
typedef enum SurfaceFlag {
    SURFACE_FLAG_NONE      = 0u,
    SURFACE_FLAG_ATTACHED  = 1u << 0,
    SURFACE_FLAG_PURGEABLE = 1u << 1,
} SurfaceFlag;

;;CLASSNAME("Surface")
;;INHERITS("GraphicsBackingImage")

/**
 * Reusable double-buffered screen presentation surface inheriting
 * GraphicsBackingImage at offset 0 (and Image at offset 0).
 *
 * GraphicsBackingImage base - active OS backing descriptor at offset 0
 * any nativeSurface         - borrowed opaque OS surface (NSView / CALayer)
 * u32 backingWidth          - fixed reusable OS backing surface width in pixels
 * u32 backingHeight         - fixed reusable OS backing surface height in pixels
 * u32 surfaceFlags          - SurfaceFlag bitmask
 * u32 activeSlot            - active double-buffer slot index (0 or 1)
 * u64 _pad                  - alignment padding to 96 bytes
 */
typedef struct Surface {
    GraphicsBackingImage base;
    any nativeSurface;
    u32 backingWidth;
    u32 backingHeight;
    u32 surfaceFlags;
    u32 activeSlot;
    u64 _pad;
} Surface;

_Static_assert(offsetof(Surface, base) == 0, "Surface must inherit GraphicsBackingImage at offset 0");
_Static_assert(sizeof(Surface) == 96, "Surface obeys 32-byte multiple (96B)");

Surface Surface_0();
Surface Surface_4(Graphics *g, any nativeSurface, u32 width, u32 height);
Surface Surface_5(Graphics *g, any nativeSurface, u32 width, u32 height, PixelFormat format);
#define Surface(...) CONSTRUCTOR_DISPATCH(Surface, __VA_ARGS__)

bool Surface_initInPlace(Surface *self, Graphics *g, any nativeSurface,
                         u32 width, u32 height, PixelFormat format);
fn Surface_deinit(Surface *self);

GraphicsBackingImage *Surface_backing(Surface *self);
const GraphicsBackingImage *Surface_backingConst(const Surface *self);
Image *Surface_image(Surface *self);
const Image *Surface_imageConst(const Surface *self);
Graphics *Surface_graphics(const Surface *self);
any Surface_nativeSurface(const Surface *self);
u32 Surface_width(const Surface *self);
u32 Surface_height(const Surface *self);
u32 Surface_backingWidth(const Surface *self);
u32 Surface_backingHeight(const Surface *self);
bool Surface_isValid(const Surface *self);
bool Surface_isAttached(const Surface *self);
bool Surface_isPurgeable(const Surface *self);

fn Surface_attach(Surface *self, any nativeSurface);
fn Surface_detach(Surface *self);
bool Surface_resize(Surface *self, u32 viewportW, u32 viewportH,
                    u32 backingW, u32 backingH);
bool Surface_clear(Surface *self, u32 clearRGBA);
bool Surface_begin(Surface *self, u32 clearRGBA);
fn Surface_setPurgeable(Surface *self, bool purgeable);
bool Surface_present(Surface *self, bool sync);
bool Surface_presentImage(Surface *self, const Image *image, bool sync);

#endif
