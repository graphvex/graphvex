/** core/surface.c -- reusable screen presentation Surface inheriting GraphicsImage. */

#include "vex.h"
#include "core/surface.h"

#include <string.h>

_Static_assert(offsetof(Surface, base) == 0, "Surface must inherit GraphicsImage at offset 0");

;;INHERITS("GraphicsImage")
;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Surface
 * ============================================================================
 * Screen presentation surface owned by graphvex/api inheriting GraphicsImage
 * at offset 0. Reuses fixed OS-backed presentation buffers (IOSurface on
 * Apple Metal, allocated outside VideoPool), clears and repaints in place
 * without destroy/rebuild churn, supports volatile purgeability when hidden,
 * and exposes Surface_present / Surface_presentImage for Frame above.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Surface (core/surface.c)
 * ============================================================================
 * Reusable presentation surface extending GraphicsImage at offset 0.
 *
 * --FIELDS-------------------------------
 * public   GraphicsImage base        embedded GraphicsImage at offset 0
 * private  Graphics     *graphics    borrows Graphics, 1:1
 * private  any           nativeSurface borrows opaque OS surface, 1:1
 * private  u32           backingWidth  fixed backing surface width in pixels
 * private  u32           backingHeight fixed backing surface height in pixels
 * private  u32           flags       SurfaceFlag state bitmask
 *
 * --CONSTRUCTORS-------------------------
 * public   Surface_0/4/5(...)        value constructors via Surface(...)
 * public   Surface_initInPlace(...)  in-place initializer for subclasses
 *
 * --FUNCTIONS----------------------------
 * public   Surface_deinit(...)       detaches nativeSurface and clears state
 * public   Surface_attach(...)       binds opaque OS surface handle
 * public   Surface_detach(...)       clears opaque OS surface handle
 * public   Surface_resize(...)       updates viewport and monotonic backing size
 * public   Surface_clear(...)        clears/zeros active surface in place
 * public   Surface_begin(...)        targets surface and clears in place
 * public   Surface_setPurgeable(...) marks backing IOSurfaces volatile/non-volatile
 * public   Surface_present(...)      submits recorded commands to screen surface
 * public   Surface_presentImage(...) presents composed GraphicsImage to screen
 *
 * --GETTERS------------------------------
 * public   Surface_image(...)        mutable base GraphicsImage pointer
 * public   Surface_imageConst(...)   const base GraphicsImage pointer
 * public   Surface_graphics(...)     borrowed Graphics context
 * public   Surface_nativeSurface(...) borrowed OS surface handle
 * public   Surface_width/height(...) active viewport dimensions
 * public   Surface_backingWidth/Height fixed backing dimensions
 * public   Surface_isValid/Attached/Purgeable state predicates
 * ============================================================================
 */

;;GETTER
GraphicsImage *Surface_image(Surface *self)
{
    if (!self)
        return nullptr;

    return &(*self).base;
}

;;GETTER
const GraphicsImage *Surface_imageConst(const Surface *self)
{
    if (!self)
        return nullptr;

    return &(*self).base;
}

;;GETTER
Graphics *Surface_graphics(const Surface *self)
{
    if (!self)
        return nullptr;

    return (*self).graphics;
}

;;SETTER
static fn Surface_setGraphicsField(Surface *self, Graphics *g)
{
    if (!self)
        return;

    (*self).graphics = g;
}

;;GETTER
any Surface_nativeSurface(const Surface *self)
{
    if (!self)
        return nullptr;

    return (*self).nativeSurface;
}

;;SETTER
static fn Surface_setNativeSurfaceField(Surface *self, any nativeSurface)
{
    if (!self)
        return;

    (*self).nativeSurface = nativeSurface;
}

;;GETTER
u32 Surface_backingWidth(const Surface *self)
{
    if (!self)
        return 0u;

    return (*self).backingWidth;
}

;;GETTER
u32 Surface_backingHeight(const Surface *self)
{
    if (!self)
        return 0u;

    return (*self).backingHeight;
}

;;SETTER
static fn Surface_setBackingFields(Surface *self, u32 w, u32 h)
{
    if (!self)
        return;

    (*self).backingWidth = w;
    (*self).backingHeight = h;
}

;;GETTER
static u32 Surface_flagsField(const Surface *self)
{
    if (!self)
        return SURFACE_FLAG_NONE;

    return (*self).flags;
}

;;SETTER
static fn Surface_setFlagsField(Surface *self, u32 flags)
{
    if (!self)
        return;

    (*self).flags = flags;
}

;;SETTER
static bool Surface_setBaseImageFields(Surface *self, u32 width, u32 height, PixelFormat format)
{
    if (!self)
        return false;

    u32 bpp = PixelFormat_bytesPerPixel(format);
    if (width == 0u || height == 0u || bpp == 0u)
        return false;

    u64 stride64 = (u64) width * (u64) bpp;
    if (stride64 > 0xFFFFFFFFULL)
        return false;

    GraphicsImage img = GraphicsImage_0();
    img.data = 1u;
    img.width = width;
    img.height = height;
    img.strideBytes = (u32) stride64;
    img.bindlessSlot = GRAPHICS_BINDLESS_DUMMY_SLOT;
    img.format = format;
    img.flags = GRAPHICS_IMAGE_NONE;
    (*self).base = img;
    return true;
}

/**
 * Initializes a Surface in place for embedding at offset 0 inside subclasses.
 * @param self          target Surface storage
 * @param g             borrowed Graphics context
 * @param nativeSurface optional opaque OS surface handle (may be nullptr until attach)
 * @param width         initial viewport width in pixels
 * @param height        initial viewport height in pixels
 * @param format        pixel format for the presentation surface
 * @return true on success, false on invalid arguments
 */
bool Surface_initInPlace(Surface *self, Graphics *g, any nativeSurface,
                         u32 width, u32 height, PixelFormat format)
{
    if (!self || !g)
        return false;

    memset(self, 0, sizeof(Surface));
    u32 safeW = width > 0u ? width : 1u;
    u32 safeH = height > 0u ? height : 1u;
    if (!Surface_setBaseImageFields(self, safeW, safeH, format))
        return false;

    Surface_setGraphicsField(self, g);
    Surface_setNativeSurfaceField(self, nativeSurface);
    Surface_setBackingFields(self, safeW, safeH);
    u32 flags = nativeSurface ? SURFACE_FLAG_ATTACHED : SURFACE_FLAG_NONE;
    Surface_setFlagsField(self, flags);
    return true;
}

;;CONSTRUCTOR
/**
 * Constructs a zeroed invalid Surface value.
 * @return zeroed Surface
 */
Surface Surface_0()
{
    Surface s;
    memset(&s, 0, sizeof(Surface));
    return s;
}

;;CONSTRUCTOR
/**
 * Constructs an RGBA8 Surface bound to a Graphics context and native surface.
 * @param g             active Graphics context
 * @param nativeSurface opaque OS surface handle
 * @param width         initial viewport width in pixels
 * @param height        initial viewport height in pixels
 * @return initialized Surface value
 */
Surface Surface_4(Graphics *g, any nativeSurface, u32 width, u32 height)
{
    return Surface_5(g, nativeSurface, width, height, PIXEL_FORMAT_RGBA8_UNORM);
}

;;CONSTRUCTOR
/**
 * Constructs a Surface with an explicit PixelFormat.
 * @param g             active Graphics context
 * @param nativeSurface opaque OS surface handle
 * @param width         initial viewport width in pixels
 * @param height        initial viewport height in pixels
 * @param format        pixel format
 * @return initialized Surface value
 */
Surface Surface_5(Graphics *g, any nativeSurface, u32 width, u32 height, PixelFormat format)
{
    Surface s = Surface_0();
    if (!Surface_initInPlace(&s, g, nativeSurface, width, height, format))
        return Surface_0();

    return s;
}

;;DECONSTRUCT
/**
 * Detaches native surface state and zeroes the Surface descriptor.
 * @param self target Surface
 */
fn Surface_deinit(Surface *self)
{
    if (!self)
        return;

    Surface_detach(self);
    memset(self, 0, sizeof(Surface));
}

u32 Surface_width(const Surface *self)
{
    return GraphicsImage_width(Surface_imageConst(self));
}

u32 Surface_height(const Surface *self)
{
    return GraphicsImage_height(Surface_imageConst(self));
}

bool Surface_isValid(const Surface *self)
{
    if (!self)
        return false;

    return Surface_graphics(self) != nullptr && GraphicsImage_isValid(Surface_imageConst(self));
}

bool Surface_isAttached(const Surface *self)
{
    if (!self)
        return false;

    return Surface_nativeSurface(self) != nullptr &&
           (Surface_flagsField(self) & SURFACE_FLAG_ATTACHED) != 0u;
}

bool Surface_isPurgeable(const Surface *self)
{
    if (!self)
        return false;

    return (Surface_flagsField(self) & SURFACE_FLAG_PURGEABLE) != 0u;
}

/**
 * Binds an opaque OS presentation surface (such as NSView or CALayer).
 * @param self          target Surface
 * @param nativeSurface opaque platform surface handle
 */
fn Surface_attach(Surface *self, any nativeSurface)
{
    if (!self)
        return;

    Surface_setNativeSurfaceField(self, nativeSurface);
    u32 flags = Surface_flagsField(self);
    if (nativeSurface)
        flags |= SURFACE_FLAG_ATTACHED;
    else
        flags &= ~SURFACE_FLAG_ATTACHED;

    Surface_setFlagsField(self, flags);
}

/**
 * Detaches the currently bound native OS surface without destroying backing state.
 * @param self target Surface
 */
fn Surface_detach(Surface *self)
{
    Surface_attach(self, nullptr);
}

/**
 * Updates active viewport dimensions and monotonically grows reusable backing
 * surface dimensions without destroying and recreating on shrink.
 * @param self      target Surface
 * @param viewportW active window viewport width in pixels
 * @param viewportH active window viewport height in pixels
 * @param backingW  requested reusable backing width in pixels (0 keeps current)
 * @param backingH  requested reusable backing height in pixels (0 keeps current)
 * @return true on success, false on invalid arguments
 */
bool Surface_resize(Surface *self, u32 viewportW, u32 viewportH,
                    u32 backingW, u32 backingH)
{
    if (!self || viewportW == 0u || viewportH == 0u)
        return false;

    PixelFormat fmt = GraphicsImage_format(Surface_imageConst(self));
    if (!Surface_setBaseImageFields(self, viewportW, viewportH, fmt))
        return false;

    u32 curBW = Surface_backingWidth(self);
    u32 curBH = Surface_backingHeight(self);
    u32 nextBW = backingW > curBW ? backingW : curBW;
    u32 nextBH = backingH > curBH ? backingH : curBH;
    if (viewportW > nextBW)
        nextBW = viewportW;

    if (viewportH > nextBH)
        nextBH = viewportH;

    Surface_setBackingFields(self, nextBW, nextBH);
    return true;
}

/**
 * Clears the reused surface in place to clearRGBA without reallocating.
 * @param self      target Surface
 * @param clearRGBA packed RGBA8 clear color
 * @return true on success, false if Surface or Graphics is invalid
 */
bool Surface_clear(Surface *self, u32 clearRGBA)
{
    if (!Surface_isValid(self))
        return false;

    Graphics *g = Surface_graphics(self);
    Graphics_setTarget(g, Surface_image(self));
    return Graphics_clear(g, clearRGBA);
}

/**
 * Targets this Surface on its Graphics context and clears it in place.
 * @param self      target Surface
 * @param clearRGBA packed RGBA8 clear color
 * @return true on success, false on invalid state
 */
bool Surface_begin(Surface *self, u32 clearRGBA)
{
    return Surface_clear(self, clearRGBA);
}

/**
 * Marks the OS backing surfaces (IOSurfaces on Metal) volatile when idle or
 * occluded, or restores non-volatile state before repainting.
 * @param self      target Surface
 * @param purgeable true to mark volatile, false to restore non-volatile
 */
fn Surface_setPurgeable(Surface *self, bool purgeable)
{
    if (!self)
        return;

    Graphics *g = Surface_graphics(self);
    if (g)
        Graphics_setSurfacePurgeable(g, purgeable);

    u32 flags = Surface_flagsField(self);
    if (purgeable)
        flags |= SURFACE_FLAG_PURGEABLE;
    else
        flags &= ~SURFACE_FLAG_PURGEABLE;

    Surface_setFlagsField(self, flags);
}

/**
 * Submits recorded Graphics commands directly to this Surface's reusable OS
 * backing buffer and presents it to the attached native surface.
 * @param self target Surface
 * @param sync true for synchronous transactional present (e.g. live resize)
 * @return true on success, false on unattached surface or GPU failure
 */
bool Surface_present(Surface *self, bool sync)
{
    if (!Surface_isValid(self) || !Surface_isAttached(self))
        return false;

    Graphics *g = Surface_graphics(self);
    u32 vw = Surface_width(self);
    u32 vh = Surface_height(self);
    u32 bw = Surface_backingWidth(self);
    u32 bh = Surface_backingHeight(self);
    if (bw < vw)
        bw = vw;

    if (bh < vh)
        bh = vh;

    return Graphics_submitToSurface(g, Surface_nativeSurface(self), vw, vh, bw, bh, sync);
}

/**
 * Presents a composed GraphicsImage onto this Surface's reusable OS backing
 * buffer (used when offscreen filter passes rendered into a VideoPool image).
 * @param self  target Surface
 * @param image composed RGBA8 GraphicsImage to present
 * @param sync  true for synchronous transactional present
 * @return true on success, false on invalid handles or GPU failure
 */
bool Surface_presentImage(Surface *self, const GraphicsImage *image, bool sync)
{
    if (!Surface_isValid(self) || !Surface_isAttached(self) || !GraphicsImage_isValid(image))
        return false;

    Graphics *g = Surface_graphics(self);
    any nativeSurf = Surface_nativeSurface(self);
    if (sync)
        return Graphics_presentSync(g, nativeSurf, image);

    return Graphics_present(g, nativeSurf, image);
}
