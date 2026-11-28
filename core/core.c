#include "vex.h"
#include "core/core_internal.h"

#include <string.h>

#define GRAPHICS_DEFAULT_CMD_CAP 256u
#define GRAPHICS_MAX_SHADERS     256u
#define GRAPHICS_COPY_CHUNK      4096u

static GraphicsBackend s_graphicsBackendOverride = GRAPHICS_AUTO;
static bool s_graphicsBackendLatched = false;

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Graphics
 * ============================================================================
 * Unified Graphics API context over graphvex/api/vio/ ImagePool slabs and
 * external OS GraphicsBackingImage handles. Owns the command queue buffer and
 * active backend driver seam (Metal or software UMA reference driver), records
 * Image / GraphicsImage / GraphicsBackingImage operations, shaders, clear/copy/
 * dispatch/draw commands, and submits or presents without a swapchain.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Graphics, Image, GraphicsImage, GraphicsBackingImage (core/core.c)
 * ============================================================================
 * Image consumer currency, sibling GraphicsImage and GraphicsBackingImage
 * constructors, and Graphics command-recording context.
 *
 * --CONSTANTS----------------------------
 * private  const u32   GRAPHICS_DEFAULT_CMD_CAP   initial command buffer slots
 * private  const u32   GRAPHICS_MAX_SHADERS       max software shader slots
 * private  const bytes GRAPHICS_COPY_CHUNK        staging copy chunk bytes
 *
 * --CONSTRUCTORS-------------------------
 * public   Image_0(...)                           zeroed invalid Image
 * public   GraphicsImage_0/5(...)                 zeroed and ImageArena image
 * public   GraphicsBackingImage_0/4/5(...)        zeroed, alloc, and import backing
 * public   Graphics_1/2(...)                      default/Metal and driver Graphics
 *
 * --FUNCTIONS----------------------------
 * public   Image_isValid/isGraphics/isBacking/isSubview/isPurgeable/byteSize/fitRect
 * public   Image_asGraphics/asBacking             checked sibling downcasts
 * public   GraphicsBackingImage_destroy/setPurgeable/isInUse
 * public   Graphics_destroy/attachPool/detachPool context and pool lifecycle
 * public   Graphics_crop/copy/copyTo              image view and copy operations
 * public   Graphics_registerShader/Compute/Render shader registration
 * public   Graphics_command/clear/dispatch/draw   command queue recording
 * public   Graphics_submit/submitToSurface/present execution and presentation
 *
 * --GETTERS------------------------------
 * public   Image_width/height/strideBytes/bindlessSlot/format/kind/flags
 * public   GraphicsImage_image/data/allocationBytes
 * public   GraphicsBackingImage_image/graphics/nativeBacking/planeCount/backingSlot
 * public   Graphics_pool/driverName/frameIndex/pendingCount/bindlessCount/target/scissor
 *
 * --SETTERS------------------------------
 * public   Graphics_setTarget/setScissor/resetScissor/setSyncPresent/setSurfacePurgeable
 * ============================================================================
 */

bool GRAPHICS_OVERRIDE(GraphicsBackend backend)
{
    if (s_graphicsBackendLatched) {
        THROW("GRAPHICS_OVERRIDE: must be called before ImagePool/Graphics initialization");
        return false;
    }
    s_graphicsBackendOverride = backend;
    return true;
}

GraphicsBackend GRAPHICS_BACKEND()
{
    return s_graphicsBackendOverride;
}

fn Graphics_latchBackend()
{
    s_graphicsBackendLatched = true;
}

u32 PixelFormat_bytesPerPixel(PixelFormat format)
{
    switch (format) {
        case PIXEL_FORMAT_RGBA8_UNORM:
        case PIXEL_FORMAT_R32_FLOAT:
        case PIXEL_FORMAT_R32_UINT:
        case PIXEL_FORMAT_RG16_FLOAT:
        case PIXEL_FORMAT_RGBA8_SRGB:
            return 4u;
        case PIXEL_FORMAT_RGBA16_FLOAT:
            return 8u;
        case PIXEL_FORMAT_RGBA32_FLOAT:
            return 16u;
        case PIXEL_FORMAT_R16_UNORM:
            return 2u;
        case PIXEL_FORMAT_R8_UNORM:
        case PIXEL_FORMAT_NV12:
            return 1u;
        case PIXEL_FORMAT_D32_FLOAT:
            return 4u;
        case PIXEL_FORMAT_D32_FLOAT_S8:
            return 8u;
        default:
            return 0u;
    }
}

;;CONSTRUCTOR
GraphicsRect GraphicsRect_0()
{
    GraphicsRect r;
    memset(&r, 0, sizeof(GraphicsRect));
    return r;
}

;;CONSTRUCTOR
GraphicsRect GraphicsRect_4(i32 x0, i32 y0, i32 x1, i32 y1)
{
    GraphicsRect r;
    r.x0 = x0;
    r.y0 = y0;
    r.x1 = x1;
    r.y1 = y1;
    return r;
}

bool GraphicsRect_isEmpty(GraphicsRect rect)
{
    return rect.x1 <= rect.x0 || rect.y1 <= rect.y0;
}

u32 GraphicsRect_width(GraphicsRect rect)
{
    if (GraphicsRect_isEmpty(rect))
        return 0u;

    return (u32) (rect.x1 - rect.x0);
}

u32 GraphicsRect_height(GraphicsRect rect)
{
    if (GraphicsRect_isEmpty(rect))
        return 0u;

    return (u32) (rect.y1 - rect.y0);
}

GraphicsRect GraphicsRect_intersect(GraphicsRect a, GraphicsRect b)
{
    i32 x0 = a.x0 > b.x0 ? a.x0 : b.x0;
    i32 y0 = a.y0 > b.y0 ? a.y0 : b.y0;
    i32 x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    i32 y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    if (x1 <= x0 || y1 <= y0)
        return GraphicsRect_0();

    return GraphicsRect_4(x0, y0, x1, y1);
}

;;CONSTRUCTOR
Image Image_0()
{
    Image img;
    memset(&img, 0, sizeof(Image));
    return img;
}

;;GETTER
u32 Image_width(const Image *self)
{
    if (!self)
        return 0u;

    return (*self).width;
}

;;GETTER
u32 Image_height(const Image *self)
{
    if (!self)
        return 0u;

    return (*self).height;
}

;;GETTER
u32 Image_strideBytes(const Image *self)
{
    if (!self)
        return 0u;

    return (*self).strideBytes;
}

;;GETTER
u32 Image_bindlessSlot(const Image *self)
{
    if (!self)
        return GRAPHICS_BINDLESS_DUMMY_SLOT;

    return (*self).bindlessSlot;
}

;;GETTER
PixelFormat Image_format(const Image *self)
{
    if (!self)
        return PIXEL_FORMAT_RGBA8_UNORM;

    return (*self).format;
}

;;GETTER
ImageKind Image_kind(const Image *self)
{
    if (!self)
        return IMAGE_KIND_NONE;

    return (*self).kind;
}

;;GETTER
u32 Image_flags(const Image *self)
{
    if (!self)
        return IMAGE_FLAG_NONE;

    return (*self).flags;
}

;;SETTER
static fn Image_setFlags(Image *self, u32 flags)
{
    if (!self)
        return;

    (*self).flags = flags;
}

bool Image_isGraphics(const Image *self)
{
    return Image_kind(self) == IMAGE_KIND_GRAPHICS;
}

bool Image_isBacking(const Image *self)
{
    return Image_kind(self) == IMAGE_KIND_BACKING;
}

bool Image_isValid(const Image *self)
{
    if (!self)
        return false;

    ImageKind k = Image_kind(self);
    if (k != IMAGE_KIND_GRAPHICS && k != IMAGE_KIND_BACKING)
        return false;

    return Image_width(self) > 0u &&
           Image_height(self) > 0u &&
           Image_strideBytes(self) > 0u;
}

bool Image_isSubview(const Image *self)
{
    return (Image_flags(self) & IMAGE_FLAG_SUBVIEW) != 0u;
}

bool Image_isPurgeable(const Image *self)
{
    return (Image_flags(self) & IMAGE_FLAG_PURGEABLE) != 0u;
}

bytes Image_byteSize(const Image *self)
{
    if (!Image_isValid(self))
        return 0u;

    bytes yBytes = (bytes) Image_strideBytes(self) * (bytes) Image_height(self);
    if (Image_format(self) == PIXEL_FORMAT_NV12) {
        u32 uvW = (Image_width(self) + 1u) / 2u;
        u32 uvH = (Image_height(self) + 1u) / 2u;
        return yBytes + (bytes) uvW * (bytes) uvH * 2u;
    }
    return yBytes;
}

fn Image_fitRect(const Image *self, f32 dstX, f32 dstY, f32 dstW, f32 dstH,
                 f32 *outX, f32 *outY, f32 *outW, f32 *outH)
{
    u32 iw = Image_width(self);
    u32 ih = Image_height(self);
    if (iw == 0u || ih == 0u || dstW <= 0.0f || dstH <= 0.0f) {
        if (outX)
            *outX = dstX;

        if (outY)
            *outY = dstY;

        if (outW)
            *outW = 0.0f;

        if (outH)
            *outH = 0.0f;

        return;
    }
    f32 srcAspect = (f32) iw / (f32) ih;
    f32 dstAspect = dstW / dstH;
    f32 w = dstW;
    f32 h = dstH;
    if (srcAspect > dstAspect) {
        h = dstW / srcAspect;
    } else {
        w = dstH * srcAspect;
    }
    if (outX)
        *outX = dstX + (dstW - w) * 0.5f;

    if (outY)
        *outY = dstY + (dstH - h) * 0.5f;

    if (outW)
        *outW = w;

    if (outH)
        *outH = h;
}

GraphicsImage *Image_asGraphics(Image *self)
{
    if (!Image_isGraphics(self))
        return nullptr;

    return (GraphicsImage*) self;
}

const GraphicsImage *Image_asGraphicsConst(const Image *self)
{
    if (!Image_isGraphics(self))
        return nullptr;

    return (const GraphicsImage*) self;
}

GraphicsBackingImage *Image_asBacking(Image *self)
{
    if (!Image_isBacking(self))
        return nullptr;

    return (GraphicsBackingImage*) self;
}

const GraphicsBackingImage *Image_asBackingConst(const Image *self)
{
    if (!Image_isBacking(self))
        return nullptr;

    return (const GraphicsBackingImage*) self;
}

;;GETTER
Image *GraphicsImage_image(GraphicsImage *self)
{
    if (!self)
        return nullptr;

    return &(*self).base;
}

;;GETTER
const Image *GraphicsImage_imageConst(const GraphicsImage *self)
{
    if (!self)
        return nullptr;

    return &(*self).base;
}

;;GETTER
vpointer GraphicsImage_data(const GraphicsImage *self)
{
    if (!self)
        return 0u;

    return (*self).data;
}

;;GETTER
bytes GraphicsImage_allocationBytes(const GraphicsImage *self)
{
    if (!self)
        return 0u;

    return (*self).allocationBytes;
}

u32 GraphicsImage_width(const GraphicsImage *self)
{
    return Image_width(GraphicsImage_imageConst(self));
}

u32 GraphicsImage_height(const GraphicsImage *self)
{
    return Image_height(GraphicsImage_imageConst(self));
}

u32 GraphicsImage_strideBytes(const GraphicsImage *self)
{
    return Image_strideBytes(GraphicsImage_imageConst(self));
}

u32 GraphicsImage_bindlessSlot(const GraphicsImage *self)
{
    return Image_bindlessSlot(GraphicsImage_imageConst(self));
}

PixelFormat GraphicsImage_format(const GraphicsImage *self)
{
    return Image_format(GraphicsImage_imageConst(self));
}

u32 GraphicsImage_flags(const GraphicsImage *self)
{
    return Image_flags(GraphicsImage_imageConst(self));
}

bool GraphicsImage_isValid(const GraphicsImage *self)
{
    if (!self)
        return false;

    const Image *base = GraphicsImage_imageConst(self);
    return Image_isGraphics(base) &&
           Image_isValid(base) &&
           GraphicsImage_data(self) != 0u;
}

bool GraphicsImage_isSubview(const GraphicsImage *self)
{
    return Image_isSubview(GraphicsImage_imageConst(self));
}

bytes GraphicsImage_byteSize(const GraphicsImage *self)
{
    if (!GraphicsImage_isValid(self))
        return 0u;

    return Image_byteSize(GraphicsImage_imageConst(self));
}

fn GraphicsImage_fitRect(const GraphicsImage *self, f32 dstX, f32 dstY, f32 dstW, f32 dstH,
                         f32 *outX, f32 *outY, f32 *outW, f32 *outH)
{
    Image_fitRect(GraphicsImage_imageConst(self), dstX, dstY, dstW, dstH, outX, outY, outW, outH);
}

;;GETTER
Image *GraphicsBackingImage_image(GraphicsBackingImage *self)
{
    if (!self)
        return nullptr;

    return &(*self).base;
}

;;GETTER
const Image *GraphicsBackingImage_imageConst(const GraphicsBackingImage *self)
{
    if (!self)
        return nullptr;

    return &(*self).base;
}

;;GETTER
Graphics *GraphicsBackingImage_graphics(const GraphicsBackingImage *self)
{
    if (!self)
        return nullptr;

    return (*self).graphics;
}

;;GETTER
any GraphicsBackingImage_nativeBacking(const GraphicsBackingImage *self)
{
    if (!self)
        return nullptr;

    return (*self).nativeBacking;
}

;;GETTER
u32 GraphicsBackingImage_planeCount(const GraphicsBackingImage *self)
{
    if (!self)
        return 0u;

    return (*self).planeCount;
}

;;GETTER
u32 GraphicsBackingImage_backingSlot(const GraphicsBackingImage *self)
{
    if (!self)
        return 0u;

    return (*self).backingSlot;
}

u32 GraphicsBackingImage_width(const GraphicsBackingImage *self)
{
    return Image_width(GraphicsBackingImage_imageConst(self));
}

u32 GraphicsBackingImage_height(const GraphicsBackingImage *self)
{
    return Image_height(GraphicsBackingImage_imageConst(self));
}

u32 GraphicsBackingImage_strideBytes(const GraphicsBackingImage *self)
{
    return Image_strideBytes(GraphicsBackingImage_imageConst(self));
}

PixelFormat GraphicsBackingImage_format(const GraphicsBackingImage *self)
{
    return Image_format(GraphicsBackingImage_imageConst(self));
}

bool GraphicsBackingImage_isValid(const GraphicsBackingImage *self)
{
    if (!self)
        return false;

    const Image *base = GraphicsBackingImage_imageConst(self);
    if (!Image_isBacking(base) || !Image_isValid(base))
        return false;

    return GraphicsBackingImage_backingSlot(self) != 0u ||
           GraphicsBackingImage_nativeBacking(self) != nullptr ||
           GraphicsBackingImage_graphics(self) != nullptr;
}

bool GraphicsBackingImage_isPurgeable(const GraphicsBackingImage *self)
{
    return Image_isPurgeable(GraphicsBackingImage_imageConst(self));
}

;;GETTER
static ImagePool *Graphics_getPool(const Graphics *g)
{
    if (!g)
        return nullptr;

    return (*g).pool;
}

;;SETTER
static fn Graphics_setPool(Graphics *g, ImagePool *pool)
{
    if (!g)
        return;

    (*g).pool = pool;
}

;;GETTER
static u32 Graphics_getAuxPoolCount(const Graphics *g)
{
    if (!g)
        return 0u;

    return (*g).auxPoolCount;
}

;;SETTER
static fn Graphics_setAuxPoolCount(Graphics *g, u32 count)
{
    if (!g)
        return;

    (*g).auxPoolCount = count;
}

;;GETTER
static const ImagePool *Graphics_getAuxPoolAt(const Graphics *g, u32 idx)
{
    if (!g || idx >= GRAPHICS_MAX_AUX_POOLS)
        return nullptr;

    return (*g).auxPools[idx];
}

;;SETTER
static fn Graphics_setAuxPoolAt(Graphics *g, u32 idx, const ImagePool *pool)
{
    if (!g || idx >= GRAPHICS_MAX_AUX_POOLS)
        return;

    (*g).auxPools[idx] = pool;
}

;;GETTER
static GraphicsDriver Graphics_getDriver(const Graphics *g)
{
    GraphicsDriver empty;
    memset(&empty, 0, sizeof(GraphicsDriver));
    if (!g)
        return empty;

    return (*g).driver;
}

;;SETTER
static fn Graphics_setDriver(Graphics *g, GraphicsDriver driver)
{
    if (!g)
        return;

    (*g).driver = driver;
}

;;GETTER
static Image *Graphics_getTarget(const Graphics *g)
{
    if (!g)
        return nullptr;

    return (*g).target;
}

;;SETTER
static fn Graphics_setTargetInternal(Graphics *g, Image *target)
{
    if (!g)
        return;

    (*g).target = target;
}

;;GETTER
static GraphicsRect Graphics_getScissor(const Graphics *g)
{
    if (!g)
        return GraphicsRect_0();

    return (*g).scissor;
}

;;SETTER
static fn Graphics_setScissorInternal(Graphics *g, GraphicsRect rect)
{
    if (!g)
        return;

    (*g).scissor = rect;
}

;;GETTER
static bool Graphics_getScissorActive(const Graphics *g)
{
    if (!g)
        return false;

    return (*g).scissorActive;
}

;;SETTER
static fn Graphics_setScissorActive(Graphics *g, bool active)
{
    if (!g)
        return;

    (*g).scissorActive = active;
}

;;GETTER
static u64 Graphics_getFrameIndex(const Graphics *g)
{
    if (!g)
        return 0u;

    return (*g).frameIndex;
}

;;SETTER
static fn Graphics_setFrameIndex(Graphics *g, u64 idx)
{
    if (!g)
        return;

    (*g).frameIndex = idx;
}

;;GETTER
static u32 Graphics_getBindlessNextSlot(const Graphics *g)
{
    if (!g)
        return 1u;

    return (*g).bindlessNextSlot;
}

;;SETTER
static fn Graphics_setBindlessNextSlot(Graphics *g, u32 next)
{
    if (!g)
        return;

    (*g).bindlessNextSlot = next;
}

;;GETTER
static u32 Graphics_getShaderCount(const Graphics *g)
{
    if (!g)
        return 0u;

    return (*g).shaderCount;
}

;;SETTER
static fn Graphics_setShaderCount(Graphics *g, u32 count)
{
    if (!g)
        return;

    (*g).shaderCount = count;
}

;;SETTER
static fn Graphics_setSwBackingNextSlot(Graphics *g, u32 next)
{
    if (!g)
        return;

    (*g).swBackingNextSlot = next;
}

;;GETTER
static GraphicsCommand *Graphics_getCommands(const Graphics *g)
{
    if (!g)
        return nullptr;

    return (*g).commands;
}

;;SETTER
static fn Graphics_setCommands(Graphics *g, GraphicsCommand *cmds)
{
    if (!g)
        return;

    (*g).commands = cmds;
}

;;GETTER
static u32 Graphics_getCommandCount(const Graphics *g)
{
    if (!g)
        return 0u;

    return (*g).commandCount;
}

;;SETTER
static fn Graphics_setCommandCount(Graphics *g, u32 count)
{
    if (!g)
        return;

    (*g).commandCount = count;
}

;;GETTER
static u32 Graphics_getCommandCap(const Graphics *g)
{
    if (!g)
        return 0u;

    return (*g).commandCap;
}

;;SETTER
static fn Graphics_setCommandCap(Graphics *g, u32 cap)
{
    if (!g)
        return;

    (*g).commandCap = cap;
}

;;GETTER
static SecureHandle Graphics_getSelfHandle(const Graphics *g)
{
    if (!g)
        return SecureHandle_zero();

    return (*g).selfHandle;
}

;;SETTER
static fn Graphics_setSelfHandle(Graphics *g, SecureHandle h)
{
    if (!g)
        return;

    (*g).selfHandle = h;
}

;;GETTER
static SecureHandle Graphics_getCmdHandle(const Graphics *g)
{
    if (!g)
        return SecureHandle_zero();

    return (*g).cmdHandle;
}

;;SETTER
static fn Graphics_setCmdHandle(Graphics *g, SecureHandle h)
{
    if (!g)
        return;

    (*g).cmdHandle = h;
}

static const ImagePool *swResolvePool(const Graphics *g, const ImagePool *primary,
                                      vpointer addr, bytes len)
{
    if (primary && ImageMemory_contains(primary, addr, len))
        return primary;

    if (!g)
        return nullptr;

    u32 nAux = Graphics_getAuxPoolCount(g);
    for (u32 i = 0u; i < nAux; i++) {
        const ImagePool *aux = Graphics_getAuxPoolAt(g, i);
        if (aux && ImageMemory_contains(aux, addr, len))
            return aux;
    }
    return nullptr;
}

static bool swExecClear(const Graphics *g, const ImagePool *pool, GraphicsCommand cmd)
{
    Image dst = cmd.target;
    if (!Image_isGraphics(&dst) || !Image_isValid(&dst) || cmd.targetData == 0u)
        return false;

    u32 bpp = PixelFormat_bytesPerPixel(dst.format);
    if (bpp != 4u)
        return false;

    GraphicsRect bounds = GraphicsRect_4(0, 0, (i32) dst.width, (i32) dst.height);
    GraphicsRect clip = GraphicsRect_isEmpty(cmd.scissor)
        ? bounds
        : GraphicsRect_intersect(bounds, cmd.scissor);
    if (GraphicsRect_isEmpty(clip))
        return true;

    const ImagePool *dstPool = swResolvePool(g, pool, cmd.targetData, Image_byteSize(&dst));
    if (!dstPool)
        return false;

    u8 px[4];
    px[0] = ColorRGBA_red(cmd.clearColorRGBA);
    px[1] = ColorRGBA_green(cmd.clearColorRGBA);
    px[2] = ColorRGBA_blue(cmd.clearColorRGBA);
    px[3] = ColorRGBA_alpha(cmd.clearColorRGBA);

    u8 *base = (u8*) ImageMemory_map(dstPool, cmd.targetData);
    if (!base)
        return false;

    for (i32 y = clip.y0; y < clip.y1; y++) {
        u8 *row = base + (bytes) y * (bytes) dst.strideBytes + (bytes) clip.x0 * 4u;
        for (i32 x = clip.x0; x < clip.x1; x++) {
            memcpy(row, px, 4u);
            row += 4u;
        }
    }
    return true;
}

static bool swExecCopy(const Graphics *g, const ImagePool *pool, GraphicsCommand cmd)
{
    Image dst = cmd.target;
    Image src = cmd.source;
    if (!Image_isGraphics(&dst) || !Image_isValid(&dst) || cmd.targetData == 0u)
        return false;

    if (!Image_isValid(&src))
        return false;

    if (Image_isBacking(&src))
        return true;

    if (cmd.sourceData == 0u || dst.format != src.format)
        return false;

    u32 bpp = PixelFormat_bytesPerPixel(dst.format);
    if (bpp == 0u)
        return false;

    i32 dstX = cmd.dstX;
    i32 dstY = cmd.dstY;
    GraphicsRect dstBounds = GraphicsRect_4(0, 0, (i32) dst.width, (i32) dst.height);
    GraphicsRect placed = GraphicsRect_4(dstX, dstY, dstX + (i32) src.width, dstY + (i32) src.height);
    GraphicsRect clip = GraphicsRect_intersect(dstBounds, placed);
    if (!GraphicsRect_isEmpty(cmd.scissor))
        clip = GraphicsRect_intersect(clip, cmd.scissor);

    if (GraphicsRect_isEmpty(clip))
        return true;

    const ImagePool *dstPool = swResolvePool(g, pool, cmd.targetData, Image_byteSize(&dst));
    const ImagePool *srcPool = swResolvePool(g, pool, cmd.sourceData, Image_byteSize(&src));
    if (!dstPool || !srcPool)
        return false;

    u32 copyCols = GraphicsRect_width(clip);
    bytes rowBytes = (bytes) copyCols * (bytes) bpp;
    i32 srcStartX = clip.x0 - dstX;
    i32 srcStartY = clip.y0 - dstY;

    u8 *dstBase = (u8*) ImageMemory_map(dstPool, cmd.targetData);
    u8 *srcBase = (u8*) ImageMemory_map(srcPool, cmd.sourceData);
    if (dstBase && srcBase) {
        for (i32 y = clip.y0; y < clip.y1; y++) {
            i32 sy = srcStartY + (y - clip.y0);
            u8 *dRow = dstBase + (bytes) y * (bytes) dst.strideBytes + (bytes) clip.x0 * (bytes) bpp;
            u8 *sRow = srcBase + (bytes) sy * (bytes) src.strideBytes + (bytes) srcStartX * (bytes) bpp;
            memmove(dRow, sRow, rowBytes);
        }
        return true;
    }

    u8 chunk[GRAPHICS_COPY_CHUNK];
    for (i32 y = clip.y0; y < clip.y1; y++) {
        i32 sy = srcStartY + (y - clip.y0);
        bytes done = 0u;
        while (done < rowBytes) {
            bytes step = rowBytes - done;
            if (step > GRAPHICS_COPY_CHUNK)
                step = GRAPHICS_COPY_CHUNK;

            vpointer sAddr = cmd.sourceData + (u64) sy * (u64) src.strideBytes + (u64) srcStartX * (u64) bpp + done;
            vpointer dAddr = cmd.targetData + (u64) y * (u64) dst.strideBytes + (u64) clip.x0 * (u64) bpp + done;
            if (!ImageMemory_read(srcPool, chunk, sAddr, step) ||
                !ImageMemory_write(dstPool, dAddr, chunk, step)) {
                return false;
            }
            done += step;
        }
    }
    return true;
}

static bool swSubmitCommands(any context, const ImagePool *pool,
                             const GraphicsCommand *cmds, u32 count)
{
    if (!pool)
        return false;

    const Graphics *g = (const Graphics*) context;
    for (u32 i = 0u; i < count; i++) {
        GraphicsCommand c = cmds[i];
        switch (c.kind) {
            case GRAPHICS_CMD_CLEAR:
                if (!swExecClear(g, pool, c))
                    return false;
                break;
            case GRAPHICS_CMD_COPY:
                if (!swExecCopy(g, pool, c))
                    return false;
                break;
            case GRAPHICS_CMD_DISPATCH:
            case GRAPHICS_CMD_DRAW:
            case GRAPHICS_CMD_PRESENT:
                break;
            default:
                return false;
        }
    }
    return true;
}

static bool swPresentImage([[maybe_unused]] any context, const ImagePool *pool,
                           [[maybe_unused]] any nativeSurface, const Image *image)
{
    return pool != nullptr && Image_isValid(image);
}

static GraphicsDriver swDefaultDriver(Graphics *g)
{
    GraphicsDriver d;
    memset(&d, 0, sizeof(GraphicsDriver));
    d.name = "software-uma";
    d.context = g;
    d.submitCommands = swSubmitCommands;
    d.presentImage = swPresentImage;
    return d;
}

static GraphicsCommand *Graphics_initCommandBuffer(u32 cap, SecureHandle *outHandle)
{
    SecureMemoryArena *sa = Graphvex_secureArena();
    SecureHandle h = SecureMemoryArena_alloc(sa, 0u, (bytes) cap * sizeof(GraphicsCommand));
    if (!SecureHandle_isValid(h))
        return nullptr;

    if (outHandle)
        *outHandle = h;

    return (GraphicsCommand*) SecureMemoryArena_resolve(sa, h);
}

;;DECONSTRUCT
static fn Graphics_commands_destroy(SecureHandle cmdHandle)
{
    if (SecureHandle_isValid(cmdHandle))
        (discard) SecureMemoryArena_free(Graphvex_secureArena(), cmdHandle);
}

static Graphics *Graphics_init(ImagePool *pool, const GraphicsDriver *driver)
{
    if (!pool)
        return nullptr;

    SecureMemoryArena *sa = Graphvex_secureArena();
    SecureHandle selfHandle = SecureMemoryArena_alloc(sa, 0u, sizeof(Graphics));
    if (!SecureHandle_isValid(selfHandle))
        return nullptr;

    Graphics *g = (Graphics*) SecureMemoryArena_resolve(sa, selfHandle);
    if (!g) {
        (discard) SecureMemoryArena_free(sa, selfHandle);
        return nullptr;
    }
    memset(g, 0, sizeof(Graphics));
    Graphics_setSelfHandle(g, selfHandle);

    SecureHandle cmdHandle = SecureHandle_zero();
    GraphicsCommand *cmds = Graphics_initCommandBuffer(GRAPHICS_DEFAULT_CMD_CAP, &cmdHandle);
    if (!cmds) {
        Graphics_destroy(g);
        return nullptr;
    }

    Graphics_setPool(g, pool);
    Graphics_setDriver(g, driver ? *driver : swDefaultDriver(g));
    Graphics_setTargetInternal(g, nullptr);
    Graphics_setScissorInternal(g, GraphicsRect_0());
    Graphics_setScissorActive(g, false);
    Graphics_setFrameIndex(g, 0u);
    Graphics_setBindlessNextSlot(g, 1u);
    Graphics_setShaderCount(g, 0u);
    Graphics_setSwBackingNextSlot(g, 0u);
    Graphics_setCommands(g, cmds);
    Graphics_setCmdHandle(g, cmdHandle);
    Graphics_setCommandCount(g, 0u);
    Graphics_setCommandCap(g, GRAPHICS_DEFAULT_CMD_CAP);
    return g;
}

;;CONSTRUCTOR
Graphics *Graphics_2(ImagePool *pool, const GraphicsDriver *driver)
{
    return Graphics_init(pool, driver);
}

#if defined(VEX_PLATFORM_APPLE)
Graphics *Graphics_metal(ImagePool *pool);

static bool isMetalPoolName(string name)
{
    return name && name[0] == 'm' && name[1] == 'e' &&
           name[2] == 't' && name[3] == 'a' && name[4] == 'l' && name[5] == '\0';
}
#endif

;;CONSTRUCTOR
Graphics *Graphics_1(ImagePool *pool)
{
    Graphics_latchBackend();
#if defined(VEX_PLATFORM_APPLE)
    if (pool && isMetalPoolName(ImagePool_driverName(pool)))
        return Graphics_metal(pool);
#endif
    return Graphics_init(pool, nullptr);
}

;;DECONSTRUCT
fn Graphics_destroy(Graphics *g)
{
    if (!g)
        return;

    SecureHandle selfHandle = Graphics_getSelfHandle(g);
    GraphicsDriver d = Graphics_getDriver(g);
    if (d.destroyContext)
        d.destroyContext(d.context);

    Graphics_commands_destroy(Graphics_getCmdHandle(g));
    if (SecureHandle_isValid(selfHandle))
        (discard) SecureMemoryArena_free(Graphvex_secureArena(), selfHandle);
}

ImagePool *Graphics_pool(const Graphics *g)
{
    return Graphics_getPool(g);
}

const ImagePool *Graphics_resolvePool(const Graphics *g, vpointer addr, bytes sizeBytes)
{
    if (!g || addr == 0u)
        return nullptr;

    return swResolvePool(g, Graphics_getPool(g), addr, sizeBytes);
}

bool Graphics_attachPool(Graphics *g, const ImagePool *auxPool)
{
    if (!g || !auxPool)
        return false;

    if (auxPool == Graphics_getPool(g))
        return true;

    u32 count = Graphics_getAuxPoolCount(g);
    for (u32 i = 0u; i < count; i++) {
        if (Graphics_getAuxPoolAt(g, i) == auxPool)
            return true;
    }
    if (count >= GRAPHICS_MAX_AUX_POOLS)
        return false;

    Graphics_setAuxPoolAt(g, count, auxPool);
    Graphics_setAuxPoolCount(g, count + 1u);

    GraphicsDriver d = Graphics_getDriver(g);
    if (d.attachPool)
        (discard) d.attachPool(d.context, auxPool);

    return true;
}

fn Graphics_detachPool(Graphics *g, const ImagePool *auxPool)
{
    if (!g || !auxPool)
        return;

    u32 count = Graphics_getAuxPoolCount(g);
    for (u32 i = 0u; i < count; i++) {
        if (Graphics_getAuxPoolAt(g, i) == auxPool) {
            for (u32 j = i; j + 1u < count; j++)
                Graphics_setAuxPoolAt(g, j, Graphics_getAuxPoolAt(g, j + 1u));

            Graphics_setAuxPoolAt(g, count - 1u, nullptr);
            Graphics_setAuxPoolCount(g, count - 1u);
            GraphicsDriver d = Graphics_getDriver(g);
            if (d.detachPool)
                d.detachPool(d.context, auxPool);
            return;
        }
    }
}

string Graphics_driverName(const Graphics *g)
{
    GraphicsDriver d = Graphics_getDriver(g);
    return d.name ? d.name : "unknown";
}

u64 Graphics_frameIndex(const Graphics *g)
{
    return Graphics_getFrameIndex(g);
}

u32 Graphics_pendingCount(const Graphics *g)
{
    return Graphics_getCommandCount(g);
}

u32 Graphics_bindlessCount(const Graphics *g)
{
    u32 next = Graphics_getBindlessNextSlot(g);
    return next > 1u ? next - 1u : 0u;
}

;;GETTER
static u32 *Graphics_bindlessNextSlotPtr(Graphics *g)
{
    if (!g)
        return nullptr;

    return &(*g).bindlessNextSlot;
}

;;GETTER
static u32 *Graphics_swBackingNextSlotPtr(Graphics *g)
{
    if (!g)
        return nullptr;

    return &(*g).swBackingNextSlot;
}

static u32 Graphics_nextBindlessSlot(Graphics *g)
{
    u32 *ptr = Graphics_bindlessNextSlotPtr(g);
    if (!ptr)
        return GRAPHICS_BINDLESS_DUMMY_SLOT;

    return __atomic_fetch_add(ptr, 1u, __ATOMIC_RELAXED);
}

static u32 Graphics_nextSwBackingSlot(Graphics *g)
{
    u32 *ptr = Graphics_swBackingNextSlotPtr(g);
    if (!ptr)
        return 0u;

    return __atomic_add_fetch(ptr, 1u, __ATOMIC_RELAXED);
}

static u32 gfxAllocBindlessSlot(Graphics *g)
{
    return Graphics_nextBindlessSlot(g);
}

;;CONSTRUCTOR
GraphicsImage GraphicsImage_0()
{
    GraphicsImage empty;
    memset(&empty, 0, sizeof(GraphicsImage));
    return empty;
}

;;CONSTRUCTOR
GraphicsImage GraphicsImage_3(u32 width, u32 height, PixelFormat format)
{
    GraphicsImage img = GraphicsImage_0();
    if (width == 0u || height == 0u)
        return img;

    u32 bpp = PixelFormat_bytesPerPixel(format);
    if (bpp == 0u)
        return img;

    u64 stride64 = (u64) width * (u64) bpp;
    if (stride64 > 0xFFFFFFFFULL)
        return img;

    img.base.width = width;
    img.base.height = height;
    img.base.strideBytes = (u32) stride64;
    img.base.bindlessSlot = GRAPHICS_BINDLESS_DUMMY_SLOT;
    img.base.format = format;
    img.base.kind = IMAGE_KIND_GRAPHICS;
    img.base.flags = IMAGE_FLAG_NONE;
    return img;
}

;;CONSTRUCTOR
GraphicsImage GraphicsImage_5(Graphics *g, ImageArena *arena,
                              u32 width, u32 height, PixelFormat format)
{
    GraphicsImage empty = GraphicsImage_0();
    if (!g || !arena || width == 0u || height == 0u)
        return empty;

    u32 bpp = PixelFormat_bytesPerPixel(format);
    if (bpp == 0u)
        return empty;

    u64 stride64 = (u64) width * (u64) bpp;
    u64 total64 = stride64 * (u64) height;
    if (format == PIXEL_FORMAT_NV12) {
        u64 uvW = (u64) ((width + 1u) / 2u);
        u64 uvH = (u64) ((height + 1u) / 2u);
        total64 += uvW * uvH * 2ULL;
    }
    if (stride64 > 0xFFFFFFFFULL || total64 == 0u)
        return empty;

    vpointer addr = ImageArena_alloc(arena, (bytes) total64, IMAGE_MEMORY_DEFAULT_ALIGN);
    if (addr == 0u)
        return empty;

    GraphicsImage img = GraphicsImage_0();
    img.base.width = width;
    img.base.height = height;
    img.base.strideBytes = (u32) stride64;
    img.base.bindlessSlot = gfxAllocBindlessSlot(g);
    img.base.format = format;
    img.base.kind = IMAGE_KIND_GRAPHICS;
    img.base.flags = IMAGE_FLAG_NONE;
    img.data = addr;
    img.allocationBytes = (bytes) total64;
    return img;
}

;;CONSTRUCTOR
GraphicsBackingImage GraphicsBackingImage_0()
{
    GraphicsBackingImage empty;
    memset(&empty, 0, sizeof(GraphicsBackingImage));
    return empty;
}

;;CONSTRUCTOR
GraphicsBackingImage GraphicsBackingImage_4(Graphics *g, u32 width, u32 height, PixelFormat format)
{
    GraphicsBackingImage empty = GraphicsBackingImage_0();
    if (!g || width == 0u || height == 0u)
        return empty;

    u32 bpp = PixelFormat_bytesPerPixel(format);
    if (bpp == 0u)
        return empty;

    GraphicsDriver d = Graphics_getDriver(g);
    any nativeBacking = nullptr;
    u32 backingSlot = 0u;
    u32 planeCount = (format == PIXEL_FORMAT_NV12) ? 2u : 1u;
    u32 strideBytes = width * bpp;

    if (d.allocBacking) {
        if (!d.allocBacking(d.context, width, height, format,
                            &nativeBacking, &backingSlot, &planeCount, &strideBytes)) {
            return empty;
        }
    } else {
        backingSlot = Graphics_nextSwBackingSlot(g);
    }

    GraphicsBackingImage img = GraphicsBackingImage_0();
    img.base.width = width;
    img.base.height = height;
    img.base.strideBytes = strideBytes;
    img.base.bindlessSlot = gfxAllocBindlessSlot(g);
    img.base.format = format;
    img.base.kind = IMAGE_KIND_BACKING;
    img.base.flags = IMAGE_FLAG_OWNED;
    img.graphics = g;
    img.nativeBacking = nativeBacking;
    img.planeCount = planeCount;
    img.backingSlot = backingSlot;
    return img;
}

;;CONSTRUCTOR
GraphicsBackingImage GraphicsBackingImage_5(Graphics *g, any nativeBacking,
                                            u32 width, u32 height, PixelFormat format)
{
    GraphicsBackingImage empty = GraphicsBackingImage_0();
    if (!g || !nativeBacking || width == 0u || height == 0u)
        return empty;

    u32 bpp = PixelFormat_bytesPerPixel(format);
    if (bpp == 0u)
        return empty;

    GraphicsDriver d = Graphics_getDriver(g);
    u32 backingSlot = 0u;
    u32 planeCount = (format == PIXEL_FORMAT_NV12) ? 2u : 1u;
    u32 strideBytes = width * bpp;

    if (d.importBacking) {
        if (!d.importBacking(d.context, nativeBacking, width, height, format, false,
                             &backingSlot, &planeCount, &strideBytes)) {
            return empty;
        }
    } else {
        backingSlot = Graphics_nextSwBackingSlot(g);
    }

    GraphicsBackingImage img = GraphicsBackingImage_0();
    img.base.width = width;
    img.base.height = height;
    img.base.strideBytes = strideBytes;
    img.base.bindlessSlot = gfxAllocBindlessSlot(g);
    img.base.format = format;
    img.base.kind = IMAGE_KIND_BACKING;
    img.base.flags = IMAGE_FLAG_NONE;
    img.graphics = g;
    img.nativeBacking = nativeBacking;
    img.planeCount = planeCount;
    img.backingSlot = backingSlot;
    return img;
}

;;DECONSTRUCT
fn GraphicsBackingImage_destroy(GraphicsBackingImage *self)
{
    if (!self)
        return;

    Graphics *g = GraphicsBackingImage_graphics(self);
    u32 slot = GraphicsBackingImage_backingSlot(self);
    if (g && slot != 0u) {
        GraphicsDriver d = Graphics_getDriver(g);
        if (d.releaseBacking)
            d.releaseBacking(d.context, slot);
    }
    memset(self, 0, sizeof(GraphicsBackingImage));
}

bool GraphicsBackingImage_setPurgeable(GraphicsBackingImage *self, bool purgeable)
{
    if (!GraphicsBackingImage_isValid(self))
        return false;

    Graphics *g = GraphicsBackingImage_graphics(self);
    u32 slot = GraphicsBackingImage_backingSlot(self);
    if (g && slot != 0u) {
        GraphicsDriver d = Graphics_getDriver(g);
        if (d.setBackingPurgeable && !d.setBackingPurgeable(d.context, slot, purgeable))
            return false;
    }

    Image *base = GraphicsBackingImage_image(self);
    u32 flags = Image_flags(base);
    if (purgeable)
        flags |= IMAGE_FLAG_PURGEABLE;
    else
        flags &= ~IMAGE_FLAG_PURGEABLE;

    Image_setFlags(base, flags);
    return true;
}

bool GraphicsBackingImage_isInUse(const GraphicsBackingImage *self)
{
    if (!GraphicsBackingImage_isValid(self))
        return false;

    Graphics *g = GraphicsBackingImage_graphics(self);
    u32 slot = GraphicsBackingImage_backingSlot(self);
    if (!g || slot == 0u)
        return false;

    GraphicsDriver d = Graphics_getDriver(g);
    if (!d.isBackingInUse)
        return false;

    return d.isBackingInUse(d.context, slot);
}

;;SETTER
static fn gfxFillCommandTarget(GraphicsCommand *cmd, const Image *target)
{
    if (!cmd || !target)
        return;

    (*cmd).target = *target;
    if (Image_isGraphics(target)) {
        const GraphicsImage *gi = Image_asGraphicsConst(target);
        (*cmd).targetData = GraphicsImage_data(gi);
        (*cmd).targetBackingSlot = 0u;
    } else if (Image_isBacking(target)) {
        const GraphicsBackingImage *bi = Image_asBackingConst(target);
        (*cmd).targetData = 0u;
        (*cmd).targetBackingSlot = GraphicsBackingImage_backingSlot(bi);
    }
}

;;SETTER
static fn gfxFillCommandSource(GraphicsCommand *cmd, const Image *source)
{
    if (!cmd || !source)
        return;

    (*cmd).source = *source;
    if (Image_isGraphics(source)) {
        const GraphicsImage *gi = Image_asGraphicsConst(source);
        (*cmd).sourceData = GraphicsImage_data(gi);
        (*cmd).sourceBackingSlot = 0u;
    } else if (Image_isBacking(source)) {
        const GraphicsBackingImage *bi = Image_asBackingConst(source);
        (*cmd).sourceData = 0u;
        (*cmd).sourceBackingSlot = GraphicsBackingImage_backingSlot(bi);
    }
}

GraphicsImage Graphics_crop(const GraphicsImage *src, i32 x0, i32 y0, i32 x1, i32 y1)
{
    GraphicsImage empty = GraphicsImage_0();
    if (!GraphicsImage_isValid(src))
        return empty;

    u32 w = GraphicsImage_width(src);
    u32 h = GraphicsImage_height(src);
    GraphicsRect clipped = GraphicsRect_intersect(
        GraphicsRect_4(0, 0, (i32) w, (i32) h),
        GraphicsRect_4(x0, y0, x1, y1));
    if (GraphicsRect_isEmpty(clipped))
        return empty;

    PixelFormat fmt = GraphicsImage_format(src);
    u32 bpp = PixelFormat_bytesPerPixel(fmt);
    u32 stride = GraphicsImage_strideBytes(src);
    vpointer base = GraphicsImage_data(src);

    GraphicsImage view = GraphicsImage_0();
    view.base.width = GraphicsRect_width(clipped);
    view.base.height = GraphicsRect_height(clipped);
    view.base.strideBytes = stride;
    view.base.bindlessSlot = GraphicsImage_bindlessSlot(src);
    view.base.format = fmt;
    view.base.kind = IMAGE_KIND_GRAPHICS;
    view.base.flags = GraphicsImage_flags(src) | IMAGE_FLAG_SUBVIEW;
    view.data = base + (u64) clipped.y0 * (u64) stride + (u64) clipped.x0 * (u64) bpp;
    view.allocationBytes = Image_byteSize(&view.base);
    return view;
}

GraphicsImage Graphics_copy_3(Graphics *g, ImageArena *arena, const Image *src)
{
    GraphicsImage empty = GraphicsImage_0();
    if (!g || !arena || !Image_isValid(src))
        return empty;

    GraphicsImage dst = GraphicsImage(
        g, arena,
        Image_width(src),
        Image_height(src),
        Image_format(src));
    if (!GraphicsImage_isValid(&dst))
        return empty;

    GraphicsCommand cmd;
    memset(&cmd, 0, sizeof(GraphicsCommand));
    cmd.kind = GRAPHICS_CMD_COPY;
    gfxFillCommandTarget(&cmd, GraphicsImage_imageConst(&dst));
    gfxFillCommandSource(&cmd, src);
    cmd.scissor = GraphicsRect_0();
    cmd.dstX = 0;
    cmd.dstY = 0;
    if (!Graphics_command(g, &cmd))
        return empty;

    return dst;
}

GraphicsImage Graphics_copy_7(Graphics *g, ImageArena *arena, const GraphicsImage *src,
                              i32 x0, i32 y0, i32 x1, i32 y1)
{
    GraphicsImage view = Graphics_crop(src, x0, y0, x1, y1);
    if (!GraphicsImage_isValid(&view))
        return view;

    return Graphics_copy_3(g, arena, GraphicsImage_imageConst(&view));
}

bool Graphics_copyTo(Graphics *g, GraphicsImage *dst, i32 dstX, i32 dstY,
                     const Image *src)
{
    if (!g || !GraphicsImage_isValid(dst) || !Image_isValid(src))
        return false;

    GraphicsCommand cmd;
    memset(&cmd, 0, sizeof(GraphicsCommand));
    cmd.kind = GRAPHICS_CMD_COPY;
    gfxFillCommandTarget(&cmd, GraphicsImage_imageConst(dst));
    gfxFillCommandSource(&cmd, src);
    cmd.scissor = Graphics_getScissorActive(g) ? Graphics_getScissor(g) : GraphicsRect_0();
    cmd.dstX = dstX;
    cmd.dstY = dstY;
    return Graphics_command(g, &cmd);
}

fn Graphics_setTarget(Graphics *g, Image *target)
{
    Graphics_setTargetInternal(g, target);
}

Image *Graphics_target(const Graphics *g)
{
    return Graphics_getTarget(g);
}

fn Graphics_setScissor(Graphics *g, i32 x0, i32 y0, i32 x1, i32 y1)
{
    Graphics_setScissorInternal(g, GraphicsRect_4(x0, y0, x1, y1));
    Graphics_setScissorActive(g, true);
}

fn Graphics_resetScissor(Graphics *g)
{
    Graphics_setScissorInternal(g, GraphicsRect_0());
    Graphics_setScissorActive(g, false);
}

GraphicsRect Graphics_scissor(const Graphics *g)
{
    return Graphics_getScissor(g);
}

u32 Graphics_registerShader(Graphics *g, string name, u32 stage)
{
    if (!g || !name || name[0] == '\0')
        return 0u;

    GraphicsDriver d = Graphics_getDriver(g);
    if (d.registerShader)
        return d.registerShader(d.context, name, name, stage);

    u32 count = Graphics_getShaderCount(g);
    if (count >= GRAPHICS_MAX_SHADERS)
        return 0u;

    u32 id = count + 1u;
    Graphics_setShaderCount(g, id);
    return id;
}

u32 Graphics_registerCompute(Graphics *g, string path, string entryName)
{
    if (!g || !path || path[0] == '\0' || !entryName || entryName[0] == '\0')
        return 0u;

    GraphicsDriver d = Graphics_getDriver(g);
    if (d.registerShader)
        return d.registerShader(d.context, path, entryName, 2u);

    u32 count = Graphics_getShaderCount(g);
    if (count >= GRAPHICS_MAX_SHADERS)
        return 0u;

    u32 id = count + 1u;
    Graphics_setShaderCount(g, id);
    return id;
}

u32 Graphics_registerRender(Graphics *g, string path, string entryPair)
{
    if (!g || !path || path[0] == '\0' || !entryPair || entryPair[0] == '\0')
        return 0u;

    GraphicsDriver d = Graphics_getDriver(g);
    if (d.registerShader)
        return d.registerShader(d.context, path, entryPair, 0u);

    u32 count = Graphics_getShaderCount(g);
    if (count >= GRAPHICS_MAX_SHADERS)
        return 0u;

    u32 id = count + 1u;
    Graphics_setShaderCount(g, id);
    return id;
}

static bool gfxEnsureCommandCapacity(Graphics *g, u32 needed)
{
    u32 cap = Graphics_getCommandCap(g);
    if (needed <= cap)
        return true;

    u32 newCap = cap > 0u ? cap * 2u : GRAPHICS_DEFAULT_CMD_CAP;
    while (newCap < needed)
        newCap *= 2u;

    SecureHandle nextHandle = SecureHandle_zero();
    GraphicsCommand *next = Graphics_initCommandBuffer(newCap, &nextHandle);
    if (!next)
        return false;

    GraphicsCommand *old = Graphics_getCommands(g);
    SecureHandle oldHandle = Graphics_getCmdHandle(g);
    u32 count = Graphics_getCommandCount(g);
    if (old && count > 0u)
        memcpy(next, old, (bytes) count * sizeof(GraphicsCommand));

    Graphics_commands_destroy(oldHandle);
    Graphics_setCommands(g, next);
    Graphics_setCmdHandle(g, nextHandle);
    Graphics_setCommandCap(g, newCap);
    return true;
}

bool Graphics_command(Graphics *g, const GraphicsCommand *cmd)
{
    if (!g || !cmd)
        return false;

    GraphicsCommand value = *cmd;
    if (value.kind == GRAPHICS_CMD_NONE)
        return false;

    u32 count = Graphics_getCommandCount(g);
    if (!gfxEnsureCommandCapacity(g, count + 1u))
        return false;

    GraphicsCommand *cmds = Graphics_getCommands(g);
    cmds[count] = value;
    Graphics_setCommandCount(g, count + 1u);
    return true;
}

bool Graphics_clear(Graphics *g, u32 colorRGBA)
{
    Image *target = Graphics_getTarget(g);
    if (!g || !Image_isValid(target))
        return false;

    GraphicsCommand cmd;
    memset(&cmd, 0, sizeof(GraphicsCommand));
    cmd.kind = GRAPHICS_CMD_CLEAR;
    gfxFillCommandTarget(&cmd, target);
    cmd.scissor = Graphics_getScissorActive(g) ? Graphics_getScissor(g) : GraphicsRect_0();
    cmd.clearColorRGBA = colorRGBA;
    return Graphics_command(g, &cmd);
}

bool Graphics_dispatchWithSource(Graphics *g, u32 shaderId, const Image *source,
                                 u32 gridX, u32 gridY, u32 gridZ,
                                 vpointer uniformAddr, bytes uniformBytes)
{
    if (!g || shaderId == 0u || gridX == 0u || gridY == 0u || gridZ == 0u)
        return false;

    GraphicsCommand cmd;
    memset(&cmd, 0, sizeof(GraphicsCommand));
    cmd.kind = GRAPHICS_CMD_DISPATCH;
    cmd.shaderId = shaderId;
    Image *target = Graphics_getTarget(g);
    if (target)
        gfxFillCommandTarget(&cmd, target);

    if (source)
        gfxFillCommandSource(&cmd, source);

    cmd.scissor = Graphics_getScissorActive(g) ? Graphics_getScissor(g) : GraphicsRect_0();
    cmd.gridX = gridX;
    cmd.gridY = gridY;
    cmd.gridZ = gridZ;
    cmd.uniformAddr = uniformAddr;
    cmd.uniformBytes = uniformBytes;
    return Graphics_command(g, &cmd);
}

bool Graphics_dispatch(Graphics *g, u32 shaderId, u32 gridX, u32 gridY, u32 gridZ,
                       vpointer uniformAddr, bytes uniformBytes)
{
    return Graphics_dispatchWithSource(g, shaderId, nullptr, gridX, gridY, gridZ,
                                       uniformAddr, uniformBytes);
}

bool Graphics_drawWithSource(Graphics *g, u32 shaderId, const Image *source,
                             vpointer vertexAddr, vpointer indexAddr,
                             u32 count, vpointer uniformAddr, bytes uniformBytes)
{
    if (!g || shaderId == 0u || vertexAddr == 0u || count == 0u)
        return false;

    GraphicsCommand cmd;
    memset(&cmd, 0, sizeof(GraphicsCommand));
    cmd.kind = GRAPHICS_CMD_DRAW;
    cmd.shaderId = shaderId;
    Image *target = Graphics_getTarget(g);
    if (target)
        gfxFillCommandTarget(&cmd, target);

    if (source)
        gfxFillCommandSource(&cmd, source);

    cmd.scissor = Graphics_getScissorActive(g) ? Graphics_getScissor(g) : GraphicsRect_0();
    cmd.vertexAddr = vertexAddr;
    cmd.indexAddr = indexAddr;
    cmd.drawCount = count;
    cmd.uniformAddr = uniformAddr;
    cmd.uniformBytes = uniformBytes;
    return Graphics_command(g, &cmd);
}

bool Graphics_draw(Graphics *g, u32 shaderId, vpointer vertexAddr, vpointer indexAddr,
                   u32 count, vpointer uniformAddr, bytes uniformBytes)
{
    return Graphics_drawWithSource(g, shaderId, nullptr, vertexAddr, indexAddr,
                                   count, uniformAddr, uniformBytes);
}

bool Graphics_submit(Graphics *g)
{
    if (!g)
        return false;

    u32 count = Graphics_getCommandCount(g);
    GraphicsDriver d = Graphics_getDriver(g);
    bool ok = true;
    if (count > 0u && d.submitCommands)
        ok = d.submitCommands(d.context, Graphics_getPool(g), Graphics_getCommands(g), count);

    Graphics_setCommandCount(g, 0u);
    if (ok)
        Graphics_setFrameIndex(g, Graphics_getFrameIndex(g) + 1u);

    return ok;
}

bool Graphics_supportsDirectSurface(const Graphics *g)
{
    if (!g)
        return false;

    return Graphics_getDriver(g).submitToSurface != nullptr;
}

bool Graphics_submitToSurface(Graphics *g, any nativeSurface,
                              u32 viewportW, u32 viewportH,
                              u32 surfaceW, u32 surfaceH, bool sync)
{
    if (!g || nativeSurface == nullptr || viewportW == 0u || viewportH == 0u)
        return false;

    GraphicsDriver d = Graphics_getDriver(g);
    if (!d.submitToSurface)
        return false;

    u32 count = Graphics_getCommandCount(g);
    Graphics_setSyncPresent(g, sync);
    bool ok = d.submitToSurface(d.context, Graphics_getPool(g), nativeSurface,
                                viewportW, viewportH, surfaceW, surfaceH,
                                Graphics_getCommands(g), count);
    Graphics_setSyncPresent(g, false);
    Graphics_setCommandCount(g, 0u);
    if (ok)
        Graphics_setFrameIndex(g, Graphics_getFrameIndex(g) + 1u);

    return ok;
}

fn Graphics_setSyncPresent(Graphics *g, bool sync)
{
    if (!g)
        return;

    GraphicsDriver d = Graphics_getDriver(g);
    if (d.setSyncPresent)
        d.setSyncPresent(d.context, sync);
}

fn Graphics_setSurfacePurgeable(Graphics *g, bool purgeable)
{
    if (!g)
        return;

    GraphicsDriver d = Graphics_getDriver(g);
    if (d.setSurfacePurgeable)
        d.setSurfacePurgeable(d.context, purgeable);
}

bool Graphics_present(Graphics *g, any nativeSurface, const Image *image)
{
    if (!g || !Image_isValid(image))
        return false;

    if (Graphics_getCommandCount(g) > 0u) {
        if (!Graphics_submit(g))
            return false;
    }
    GraphicsDriver d = Graphics_getDriver(g);
    if (!d.presentImage)
        return false;

    return d.presentImage(d.context, Graphics_getPool(g), nativeSurface, image);
}

bool Graphics_presentSync(Graphics *g, any nativeSurface, const Image *image)
{
    if (!g)
        return false;

    Graphics_setSyncPresent(g, true);
    bool ok = Graphics_present(g, nativeSurface, image);
    Graphics_setSyncPresent(g, false);
    return ok;
}
