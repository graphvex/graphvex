#ifndef GRAPHVEX_CORE_IMAGE_H
#define GRAPHVEX_CORE_IMAGE_H

/**
 * core/image.h -- Image consumer currency and its two distinct sibling flavors:
 * GraphicsImage (ImagePool/ImageArena-owned GPU buffer) and GraphicsBackingImage
 * (external OS/hardware-backed surface: IOSurface / DMA-BUF / DXGI).
 */

#include "core/pixel.h"
#include "vio/vio.h"

;;CLASSNAME("ImageKind")

typedef enum ImageKind {
    IMAGE_KIND_NONE     = 0u,
    IMAGE_KIND_GRAPHICS = 1u,
    IMAGE_KIND_BACKING  = 2u,
} ImageKind;

;;CLASSNAME("ImageFlag")

typedef enum ImageFlag {
    IMAGE_FLAG_NONE      = 0u,
    IMAGE_FLAG_SUBVIEW   = 1u << 0,
    IMAGE_FLAG_PURGEABLE = 1u << 1,
    IMAGE_FLAG_OWNED     = 1u << 2,
} ImageFlag;

;;CLASSNAME("Image")
/**
 * Universal 32-byte image consumer currency accepted by graphvex, collage,
 * meld, and darling. Both GraphicsImage and GraphicsBackingImage inherit
 * Image at offset 0 as distinct siblings.
 *
 * u32 width         - image width in native pixels
 * u32 height        - image height in native pixels
 * u32 strideBytes   - primary plane row stride in bytes
 * u32 bindlessSlot  - auto-registered bindless image slot (0 = dummy white)
 * PixelFormat format - pixel storage layout
 * ImageKind kind    - discriminator (IMAGE_KIND_GRAPHICS or IMAGE_KIND_BACKING)
 * u32 flags         - ImageFlag bitmask
 * u32 _pad          - alignment padding to 32 bytes
 */
typedef struct Image {
    u32 width;
    u32 height;
    u32 strideBytes;
    u32 bindlessSlot;
    PixelFormat format;
    ImageKind kind;
    u32 flags;
    u32 _pad;
} Image;

_Static_assert(sizeof(Image) == 32, "Image obeys 32-byte floor");

typedef struct Graphics Graphics;

;;CLASSNAME("GraphicsImage")
;;INHERITS("Image")
/**
 * Application/engine GPU buffer image owned by an ImageArena inside ImagePool.
 * Used for icons, decoded still images, offscreen passes, and zero-copy crops.
 *
 * Image base           - embedded Image consumer currency at offset 0
 * vpointer data        - 64-bit GPU virtual address inside ImagePool
 * bytes allocationBytes - allocated byte footprint in ImageArena
 * u64 _pad0            - padding to 64 bytes
 * u64 _pad1            - padding to 64 bytes
 */
typedef struct GraphicsImage {
    Image base;
    vpointer data;
    bytes allocationBytes;
    u64 _pad0;
    u64 _pad1;
} GraphicsImage;

_Static_assert(offsetof(GraphicsImage, base) == 0, "GraphicsImage must inherit Image at offset 0");
_Static_assert(sizeof(GraphicsImage) == 64, "GraphicsImage obeys 32-byte multiple (64B)");

;;CLASSNAME("GraphicsBackingImage")
;;INHERITS("Image")
/**
 * External OS/hardware-backed image living outside ImagePool (IOSurface on
 * macOS, DMA-BUF on Linux, DXGI shared handle on Windows). Sibling of
 * GraphicsImage under Image; extended by Surface and produced zero-copy by
 * datamosh video decoders.
 *
 * Image base         - embedded Image consumer currency at offset 0
 * Graphics *graphics - borrowed Graphics context owning the driver backing slot
 * any nativeBacking  - opaque OS hardware surface handle (e.g. IOSurfaceRef)
 * u32 planeCount     - number of hardware planes (1 for RGBA/BGRA, 2 for NV12)
 * u32 backingSlot    - 1-based driver backing table slot (0 when unimported)
 * u64 _pad           - padding to 64 bytes
 */
typedef struct GraphicsBackingImage {
    Image base;
    Graphics *graphics;
    any nativeBacking;
    u32 planeCount;
    u32 backingSlot;
    u64 _pad;
} GraphicsBackingImage;

_Static_assert(offsetof(GraphicsBackingImage, base) == 0, "GraphicsBackingImage must inherit Image at offset 0");
_Static_assert(sizeof(GraphicsBackingImage) == 64, "GraphicsBackingImage obeys 32-byte multiple (64B)");

Image Image_0();
#define Image(...) CONSTRUCTOR_DISPATCH(Image, __VA_ARGS__)

;;GETTER
u32 Image_width(const Image *self);
;;GETTER
u32 Image_height(const Image *self);
;;GETTER
u32 Image_strideBytes(const Image *self);
;;GETTER
u32 Image_bindlessSlot(const Image *self);
;;GETTER
PixelFormat Image_format(const Image *self);
;;GETTER
ImageKind Image_kind(const Image *self);
;;GETTER
u32 Image_flags(const Image *self);

bool Image_isValid(const Image *self);
bool Image_isGraphics(const Image *self);
bool Image_isBacking(const Image *self);
bool Image_isSubview(const Image *self);
bool Image_isPurgeable(const Image *self);
bytes Image_byteSize(const Image *self);
fn Image_fitRect(const Image *self, f32 dstX, f32 dstY, f32 dstW, f32 dstH,
                 f32 *outX, f32 *outY, f32 *outW, f32 *outH);

GraphicsImage *Image_asGraphics(Image *self);
const GraphicsImage *Image_asGraphicsConst(const Image *self);
GraphicsBackingImage *Image_asBacking(Image *self);
const GraphicsBackingImage *Image_asBackingConst(const Image *self);

GraphicsImage GraphicsImage_0();
GraphicsImage GraphicsImage_3(u32 width, u32 height, PixelFormat format);
GraphicsImage GraphicsImage_5(Graphics *g, ImageArena *arena,
                              u32 width, u32 height, PixelFormat format);
#define GraphicsImage(...) CONSTRUCTOR_DISPATCH(GraphicsImage, __VA_ARGS__)

;;GETTER
Image *GraphicsImage_image(GraphicsImage *self);
;;GETTER
const Image *GraphicsImage_imageConst(const GraphicsImage *self);
;;GETTER
vpointer GraphicsImage_data(const GraphicsImage *self);
;;GETTER
bytes GraphicsImage_allocationBytes(const GraphicsImage *self);

u32 GraphicsImage_width(const GraphicsImage *self);
u32 GraphicsImage_height(const GraphicsImage *self);
u32 GraphicsImage_strideBytes(const GraphicsImage *self);
u32 GraphicsImage_bindlessSlot(const GraphicsImage *self);
PixelFormat GraphicsImage_format(const GraphicsImage *self);
u32 GraphicsImage_flags(const GraphicsImage *self);
bool GraphicsImage_isValid(const GraphicsImage *self);
bool GraphicsImage_isSubview(const GraphicsImage *self);
bytes GraphicsImage_byteSize(const GraphicsImage *self);
fn GraphicsImage_fitRect(const GraphicsImage *self, f32 dstX, f32 dstY, f32 dstW, f32 dstH,
                         f32 *outX, f32 *outY, f32 *outW, f32 *outH);

GraphicsBackingImage GraphicsBackingImage_0();
GraphicsBackingImage GraphicsBackingImage_4(Graphics *g, u32 width, u32 height, PixelFormat format);
GraphicsBackingImage GraphicsBackingImage_5(Graphics *g, any nativeBacking,
                                            u32 width, u32 height, PixelFormat format);
#define GraphicsBackingImage(...) CONSTRUCTOR_DISPATCH(GraphicsBackingImage, __VA_ARGS__)

fn GraphicsBackingImage_destroy(GraphicsBackingImage *self);
bool GraphicsBackingImage_setPurgeable(GraphicsBackingImage *self, bool purgeable);
bool GraphicsBackingImage_isInUse(const GraphicsBackingImage *self);

;;GETTER
Image *GraphicsBackingImage_image(GraphicsBackingImage *self);
;;GETTER
const Image *GraphicsBackingImage_imageConst(const GraphicsBackingImage *self);
;;GETTER
Graphics *GraphicsBackingImage_graphics(const GraphicsBackingImage *self);
;;GETTER
any GraphicsBackingImage_nativeBacking(const GraphicsBackingImage *self);
;;GETTER
u32 GraphicsBackingImage_planeCount(const GraphicsBackingImage *self);
;;GETTER
u32 GraphicsBackingImage_backingSlot(const GraphicsBackingImage *self);

u32 GraphicsBackingImage_width(const GraphicsBackingImage *self);
u32 GraphicsBackingImage_height(const GraphicsBackingImage *self);
u32 GraphicsBackingImage_strideBytes(const GraphicsBackingImage *self);
PixelFormat GraphicsBackingImage_format(const GraphicsBackingImage *self);
bool GraphicsBackingImage_isValid(const GraphicsBackingImage *self);
bool GraphicsBackingImage_isPurgeable(const GraphicsBackingImage *self);

#endif
