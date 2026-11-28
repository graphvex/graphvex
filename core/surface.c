/** core/surface.c -- reusable double-buffered screen presentation Surface. */

#include "vex.h"
#include "core/surface.h"

#include <string.h>

_Static_assert(offsetof(Surface, base) == 0, "Surface must inherit GraphicsBackingImage at offset 0");

;;INHERITS("GraphicsBackingImage")
;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Surface
 * ============================================================================
 * Screen presentation surface owned by graphvex/api inheriting
 * GraphicsBackingImage at offset 0 (and Image at offset 0). Reuses at most
 * 2 OS-backed presentation buffers (double-buffered ceiling; triple-buffering
 * is reserved for meld 3D scenes), clears and repaints in place without
 * destroy/rebuild churn, supports volatile purgeability when hidden, and
 * exposes Surface_present / Surface_presentImage for Frame above.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Surface (core/surface.c)
 * ============================================================================
 * Reusable double-buffered presentation surface extending GraphicsBackingImage.
 *
 * --FIELDS-------------------------------
 * public   GraphicsBackingImage base     embedded GraphicsBackingImage at offset 0
 * private  any                  nativeSurface borrows opaque OS surface, 1:1
 * private  u32                  backingWidth  fixed backing width in pixels
 * private  u32                  backingHeight fixed backing height in pixels
 * private  u32                  surfaceFlags  SurfaceFlag state bitmask
 * private  u32                  activeSlot    active double-buffer slot (0 or 1)
 *
 * --CONSTRUCTORS-------------------------
 * public   Surface_0/4/5(...)            value constructors via Surface(...)
 * public   Surface_initInPlace(...)      in-place initializer for subclasses
 *
 * --FUNCTIONS----------------------------
 * public   Surface_deinit(...)           detaches nativeSurface and clears state
 * public   Surface_attach/detach(...)    binds or clears opaque OS surface handle
 * public   Surface_resize(...)           updates viewport and monotonic backing size
 * public   Surface_clear/begin(...)      targets and clears active surface in place
 * public   Surface_setPurgeable(...)     marks backing IOSurfaces volatile/non-volatile
 * public   Surface_present(...)          submits recorded commands to screen surface
 * public   Surface_presentImage(...)     presents composed Image to screen
 *
 * --GETTERS------------------------------
 * public   Surface_backing/backingConst  base GraphicsBackingImage pointer
 * public   Surface_image/imageConst      base Image currency pointer
 * public   Surface_graphics/nativeSurface/width/height/backingWidth/backingHeight
 * public   Surface_isValid/isAttached/isPurgeable
 * ============================================================================
 */

;;GETTER
GraphicsBackingImage *Surface_backing(Surface *self)
{
    if (!self)
        return nullptr;

    return &(*self).base;
}

;;GETTER
const GraphicsBackingImage *Surface_backingConst(const Surface *self)
{
    if (!self)
        return nullptr;

    return &(*self).base;
}

;;GETTER
Image *Surface_image(Surface *self)
{
    if (!self)
        return nullptr;

    return GraphicsBackingImage_image(&(*self).base);
}

;;GETTER
const Image *Surface_imageConst(const Surface *self)
{
    if (!self)
        return nullptr;

    return GraphicsBackingImage_imageConst(&(*self).base);
}

Graphics *Surface_graphics(const Surface *self)
{
    return GraphicsBackingImage_graphics(Surface_backingConst(self));
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

    return (*self).surfaceFlags;
}

;;SETTER
static fn Surface_setFlagsField(Surface *self, u32 flags)
{
    if (!self)
        return;

    (*self).surfaceFlags = flags;
}

;;GETTER
static u32 Surface_activeSlotField(const Surface *self)
{
    if (!self)
        return 0u;

    return (*self).activeSlot;
}

;;SETTER
static fn Surface_setActiveSlotField(Surface *self, u32 slot)
{
    if (!self)
        return;

    (*self).activeSlot = slot % SURFACE_MAX_BACKING_SLOTS;
}

;;SETTER
static bool Surface_setBaseBackingFields(Surface *self, Graphics *g, u32 width, u32 height, PixelFormat format)
{
    if (!self || !g)
        return false;

    u32 bpp = PixelFormat_bytesPerPixel(format);
    if (width == 0u || height == 0u || bpp == 0u)
        return false;

    u64 stride64 = (u64) width * (u64) bpp;
    if (stride64 > 0xFFFFFFFFULL)
        return false;

    GraphicsBackingImage b = GraphicsBackingImage_0();
    b.base.width = width;
    b.base.height = height;
    b.base.strideBytes = (u32) stride64;
    b.base.bindlessSlot = GRAPHICS_BINDLESS_DUMMY_SLOT;
    b.base.format = format;
    b.base.kind = IMAGE_KIND_BACKING;
    b.base.flags = IMAGE_FLAG_NONE;
    b.graphics = g;
    b.nativeBacking = nullptr;
    b.planeCount = 1u;
    b.backingSlot = 0u;
    (*self).base = b;
    return true;
}

bool Surface_initInPlace(Surface *self, Graphics *g, any nativeSurface,
                         u32 width, u32 height, PixelFormat format)
{
    if (!self || !g)
        return false;

    memset(self, 0, sizeof(Surface));
    u32 safeW = width > 0u ? width : 1u;
    u32 safeH = height > 0u ? height : 1u;
    if (!Surface_setBaseBackingFields(self, g, safeW, safeH, format))
        return false;

    Surface_setNativeSurfaceField(self, nativeSurface);
    Surface_setBackingFields(self, safeW, safeH);
    u32 flags = nativeSurface ? SURFACE_FLAG_ATTACHED : SURFACE_FLAG_NONE;
    Surface_setFlagsField(self, flags);
    Surface_setActiveSlotField(self, 0u);
    return true;
}

;;CONSTRUCTOR
Surface Surface_0()
{
    Surface s;
    memset(&s, 0, sizeof(Surface));
    return s;
}

;;CONSTRUCTOR
Surface Surface_4(Graphics *g, any nativeSurface, u32 width, u32 height)
{
    return Surface_5(g, nativeSurface, width, height, PIXEL_FORMAT_RGBA8_UNORM);
}

;;CONSTRUCTOR
Surface Surface_5(Graphics *g, any nativeSurface, u32 width, u32 height, PixelFormat format)
{
    Surface s = Surface_0();
    if (!Surface_initInPlace(&s, g, nativeSurface, width, height, format))
        return Surface_0();

    return s;
}

;;DECONSTRUCT
fn Surface_deinit(Surface *self)
{
    if (!self)
        return;

    Surface_detach(self);
    memset(self, 0, sizeof(Surface));
}

u32 Surface_width(const Surface *self)
{
    return Image_width(Surface_imageConst(self));
}

u32 Surface_height(const Surface *self)
{
    return Image_height(Surface_imageConst(self));
}

bool Surface_isValid(const Surface *self)
{
    if (!self)
        return false;

    return Surface_graphics(self) != nullptr &&
           GraphicsBackingImage_isValid(Surface_backingConst(self));
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

fn Surface_detach(Surface *self)
{
    Surface_attach(self, nullptr);
}

bool Surface_resize(Surface *self, u32 viewportW, u32 viewportH,
                    u32 backingW, u32 backingH)
{
    if (!self || viewportW == 0u || viewportH == 0u)
        return false;

    Graphics *g = Surface_graphics(self);
    PixelFormat fmt = Image_format(Surface_imageConst(self));
    if (!Surface_setBaseBackingFields(self, g, viewportW, viewportH, fmt))
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

bool Surface_clear(Surface *self, u32 clearRGBA)
{
    if (!Surface_isValid(self))
        return false;

    Graphics *g = Surface_graphics(self);
    Graphics_setTarget(g, Surface_image(self));
    return Graphics_clear(g, clearRGBA);
}

bool Surface_begin(Surface *self, u32 clearRGBA)
{
    return Surface_clear(self, clearRGBA);
}

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

    bool ok = Graphics_submitToSurface(g, Surface_nativeSurface(self), vw, vh, bw, bh, sync);
    if (ok)
        Surface_setActiveSlotField(self, Surface_activeSlotField(self) + 1u);

    return ok;
}

bool Surface_presentImage(Surface *self, const Image *image, bool sync)
{
    if (!Surface_isValid(self) || !Surface_isAttached(self) || !Image_isValid(image))
        return false;

    Graphics *g = Surface_graphics(self);
    any nativeSurf = Surface_nativeSurface(self);
    bool ok = sync
        ? Graphics_presentSync(g, nativeSurf, image)
        : Graphics_present(g, nativeSurf, image);
    if (ok)
        Surface_setActiveSlotField(self, Surface_activeSlotField(self) + 1u);

    return ok;
}
