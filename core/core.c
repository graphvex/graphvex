/** core/core.c -- unified Graphics API implementation over graphvex/api/vio/. */

#include "vex.h"
#include "core.h"
#include "nio/graphvex_memory.h"

#define GRAPHICS_DEFAULT_CMD_CAP 256u
#define GRAPHICS_MAX_SHADERS     256u
#define GRAPHICS_COPY_CHUNK      4096u

static GraphicsBackend s_graphicsBackendOverride = GRAPHICS_AUTO;
static bool s_graphicsBackendLatched = false;

/**
 * Sets the process-wide Graphics backend override before any VideoPool,
 * Graphics, or Frame is constructed.
 * @param backend requested GraphicsBackend (e.g. GRAPHICS_VULKAN, GRAPHICS_METAL)
 * @return true if set before latch, false if already latched
 */
bool GRAPHICS_OVERRIDE(GraphicsBackend backend) {
    if (s_graphicsBackendLatched) {
        THROW("GRAPHICS_OVERRIDE: must be called before VideoPool/Graphics initialization");
        return false;
    }
    s_graphicsBackendOverride = backend;
    return true;
}

/**
 * Returns the currently configured process-wide GraphicsBackend override.
 * @return active GraphicsBackend enum value
 */
GraphicsBackend GRAPHICS_BACKEND() {
    return s_graphicsBackendOverride;
}

/**
 * Latches the active GraphicsBackend override once GPU initialization begins.
 */
fn Graphics_latchBackend() {
    s_graphicsBackendLatched = true;
}

struct [[maybe_unused]] Graphics {
    VideoPool *pool;
    GraphicsDriver driver;
    GraphicsImage *target;
    GraphicsRect scissor;
    bool scissorActive;
    u64 frameIndex;
    u32 bindlessNextSlot;
    u32 shaderCount;
    GraphicsCommand *commands;
    SecureHandle selfHandle;
    SecureHandle cmdHandle;
    u32 commandCount;
    u32 commandCap;
};

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Graphics
 * ============================================================================
 * Unified Graphics API context over graphvex/api/vio/ VideoPool slabs. Owns
 * the command queue buffer and active backend driver seam (Metal or software
 * UMA reference driver), records buffer-backed image ops, shaders, clear/copy/
 * dispatch/draw commands, and submits or presents without a swapchain.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Graphics (core/core.c)
 * ============================================================================
 * Command-recording graphics context paired with a borrowed VideoPool and
 * GraphicsDriver vtable.
 *
 * --CONSTANTS----------------------------
 * private  const u32   GRAPHICS_DEFAULT_CMD_CAP   initial command buffer slots
 * private  const u32   GRAPHICS_MAX_SHADERS       max software shader slots
 * private  const bytes GRAPHICS_COPY_CHUNK        staging copy chunk bytes
 *
 * --FIELDS-------------------------------
 * private  VideoPool       *pool                  borrows VideoPool, 1:1
 * private  GraphicsDriver   driver                owns backend driver context
 * private  GraphicsImage   *target                borrows current target, 1:1
 * private  GraphicsRect     scissor               active scissor rect
 * private  bool             scissorActive         true when scissor is set
 * private  u64              frameIndex            monotonic submitted frame
 * private  u32              bindlessNextSlot      next bindless image slot
 * private  u32              shaderCount           registered shader count
 * private  GraphicsCommand *commands              owns command queue, 1:N
 * private  u32              commandCount          queued command count
 * private  u32              commandCap            allocated queue capacity
 *
 * --CONSTRUCTORS-------------------------
 * public   Graphics_1(...)                        default/Metal constructor
 * public   Graphics_2(...)                        explicit driver constructor
 * public   GraphicsImage_0/5(...)                 zeroed and buffer-backed image constructors
 *
 * --FUNCTIONS----------------------------
 * public   Graphics_destroy(...)                  releases context and queue
 * public   Graphics_crop(...)                     zero-copy sub-image view
 * public   Graphics_copy_3/7(...)                 explicit image copy
 * public   Graphics_copyTo(...)                   copies image into target
 * public   Graphics_registerShader/Compute/Render shader registration
 * public   Graphics_command/clear/dispatch/draw   command queue recording
 * public   Graphics_submit/present(...)           execution and presentation
 *
 * --GETTERS------------------------------
 * public   Graphics_pool/driverName/frameIndex    context metadata accessors
 * public   Graphics_pendingCount/bindlessCount    queue and bindless counters
 * public   Graphics_target/scissor(...)           current render state
 *
 * --SETTERS------------------------------
 * public   Graphics_setTarget/setScissor(...)     render target and scissor
 * public   Graphics_resetScissor(...)             clears active scissor
 * ============================================================================
 */

/** ---------------------------------------------------------------- PixelFormat */

u32 PixelFormat_bytesPerPixel(PixelFormat format) {
    switch (format) {
        case PIXEL_FORMAT_RGBA8_UNORM:
        case PIXEL_FORMAT_R32_FLOAT:
        case PIXEL_FORMAT_R32_UINT:
        case PIXEL_FORMAT_RG16_FLOAT:
        case PIXEL_FORMAT_RGBA8_SRGB:   return 4u;
        case PIXEL_FORMAT_RGBA16_FLOAT: return 8u;
        case PIXEL_FORMAT_RGBA32_FLOAT: return 16u;
        case PIXEL_FORMAT_R16_UNORM:    return 2u;
        case PIXEL_FORMAT_R8_UNORM:
        case PIXEL_FORMAT_NV12:         return 1u;
        case PIXEL_FORMAT_D32_FLOAT:    return 4u;
        case PIXEL_FORMAT_D32_FLOAT_S8: return 8u;
        default:                        return 0u;
    }
}

/** ---------------------------------------------------------------- GraphicsRect */

GraphicsRect GraphicsRect_0() {
    GraphicsRect r = { 0, 0, 0, 0 };
    return r;
}

GraphicsRect GraphicsRect_4(i32 x0, i32 y0, i32 x1, i32 y1) {
    GraphicsRect r = { x0, y0, x1, y1 };
    return r;
}

bool GraphicsRect_isEmpty(GraphicsRect rect) {
    return rect.x1 <= rect.x0 || rect.y1 <= rect.y0;
}

u32 GraphicsRect_width(GraphicsRect rect) {
    return GraphicsRect_isEmpty(rect) ? 0u : (u32) (rect.x1 - rect.x0);
}

u32 GraphicsRect_height(GraphicsRect rect) {
    return GraphicsRect_isEmpty(rect) ? 0u : (u32) (rect.y1 - rect.y0);
}

GraphicsRect GraphicsRect_intersect(GraphicsRect a, GraphicsRect b) {
    i32 x0 = a.x0 > b.x0 ? a.x0 : b.x0;
    i32 y0 = a.y0 > b.y0 ? a.y0 : b.y0;
    i32 x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    i32 y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    if (x1 <= x0 || y1 <= y0)
        return GraphicsRect_0();
    return GraphicsRect_4(x0, y0, x1, y1);
}

/** ---------------------------------------------------------------- GraphicsImage */

;;GETTER
vpointer GraphicsImage_data(const GraphicsImage *self) {
    return self ? (*self).data : 0u;
}

;;GETTER
u32 GraphicsImage_width(const GraphicsImage *self) {
    return self ? (*self).width : 0u;
}

;;GETTER
u32 GraphicsImage_height(const GraphicsImage *self) {
    return self ? (*self).height : 0u;
}

;;GETTER
u32 GraphicsImage_strideBytes(const GraphicsImage *self) {
    return self ? (*self).strideBytes : 0u;
}

;;GETTER
u32 GraphicsImage_bindlessSlot(const GraphicsImage *self) {
    return self ? (*self).bindlessSlot : GRAPHICS_BINDLESS_DUMMY_SLOT;
}

;;GETTER
PixelFormat GraphicsImage_format(const GraphicsImage *self) {
    return self ? (*self).format : PIXEL_FORMAT_RGBA8_UNORM;
}

;;GETTER
u32 GraphicsImage_flags(const GraphicsImage *self) {
    return self ? (*self).flags : GRAPHICS_IMAGE_NONE;
}

bool GraphicsImage_isValid(const GraphicsImage *self) {
    if (!self)
        return false;
    return GraphicsImage_data(self) != 0u &&
           GraphicsImage_width(self) > 0u &&
           GraphicsImage_height(self) > 0u &&
           GraphicsImage_strideBytes(self) > 0u;
}

bool GraphicsImage_isSubview(const GraphicsImage *self) {
    return (GraphicsImage_flags(self) & GRAPHICS_IMAGE_SUBVIEW) != 0u;
}

bytes GraphicsImage_byteSize(const GraphicsImage *self) {
    if (!GraphicsImage_isValid(self))
        return 0u;
    bytes yBytes = (bytes) GraphicsImage_strideBytes(self) * (bytes) GraphicsImage_height(self);
    if (GraphicsImage_format(self) == PIXEL_FORMAT_NV12) {
        u32 uvW = (GraphicsImage_width(self) + 1u) / 2u;
        u32 uvH = (GraphicsImage_height(self) + 1u) / 2u;
        return yBytes + (bytes) uvW * (bytes) uvH * 2u;
    }
    return yBytes;
}

fn GraphicsImage_fitRect(const GraphicsImage *self, f32 dstX, f32 dstY, f32 dstW, f32 dstH,
                         f32 *outX, f32 *outY, f32 *outW, f32 *outH) {
    u32 iw = GraphicsImage_width(self);
    u32 ih = GraphicsImage_height(self);
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

/** ---------------------------------------------------------------- Internal Graphics accessors */

;;GETTER
static VideoPool *Graphics_getPool(const Graphics *g) {
    return g ? (*g).pool : nullptr;
}

;;SETTER
static fn Graphics_setPool(Graphics *g, VideoPool *pool) {
    if (g)
        (*g).pool = pool;
}

;;GETTER
static GraphicsDriver Graphics_getDriver(const Graphics *g) {
    GraphicsDriver empty = { 0 };
    return g ? (*g).driver : empty;
}

;;SETTER
static fn Graphics_setDriver(Graphics *g, GraphicsDriver driver) {
    if (g)
        (*g).driver = driver;
}

;;GETTER
static GraphicsImage *Graphics_getTarget(const Graphics *g) {
    return g ? (*g).target : nullptr;
}

;;SETTER
static fn Graphics_setTargetInternal(Graphics *g, GraphicsImage *target) {
    if (g)
        (*g).target = target;
}

;;GETTER
static GraphicsRect Graphics_getScissor(const Graphics *g) {
    return g ? (*g).scissor : GraphicsRect_0();
}

;;SETTER
static fn Graphics_setScissorInternal(Graphics *g, GraphicsRect rect) {
    if (g)
        (*g).scissor = rect;
}

;;GETTER
static bool Graphics_getScissorActive(const Graphics *g) {
    return g ? (*g).scissorActive : false;
}

;;SETTER
static fn Graphics_setScissorActive(Graphics *g, bool active) {
    if (g)
        (*g).scissorActive = active;
}

;;GETTER
static u64 Graphics_getFrameIndex(const Graphics *g) {
    return g ? (*g).frameIndex : 0u;
}

;;SETTER
static fn Graphics_setFrameIndex(Graphics *g, u64 idx) {
    if (g)
        (*g).frameIndex = idx;
}

;;GETTER
static u32 Graphics_getBindlessNextSlot(const Graphics *g) {
    return g ? (*g).bindlessNextSlot : 1u;
}

;;SETTER
static fn Graphics_setBindlessNextSlot(Graphics *g, u32 next) {
    if (g)
        (*g).bindlessNextSlot = next;
}

;;GETTER
static u32 Graphics_getShaderCount(const Graphics *g) {
    return g ? (*g).shaderCount : 0u;
}

;;SETTER
static fn Graphics_setShaderCount(Graphics *g, u32 count) {
    if (g)
        (*g).shaderCount = count;
}

;;GETTER
static GraphicsCommand *Graphics_getCommands(const Graphics *g) {
    return g ? (*g).commands : nullptr;
}

;;SETTER
static fn Graphics_setCommands(Graphics *g, GraphicsCommand *cmds) {
    if (g)
        (*g).commands = cmds;
}

;;GETTER
static u32 Graphics_getCommandCount(const Graphics *g) {
    return g ? (*g).commandCount : 0u;
}

;;SETTER
static fn Graphics_setCommandCount(Graphics *g, u32 count) {
    if (g)
        (*g).commandCount = count;
}

;;GETTER
static u32 Graphics_getCommandCap(const Graphics *g) {
    return g ? (*g).commandCap : 0u;
}

;;SETTER
static fn Graphics_setCommandCap(Graphics *g, u32 cap) {
    if (g)
        (*g).commandCap = cap;
}

;;GETTER
static SecureHandle Graphics_getSelfHandle(const Graphics *g) {
    return g ? (*g).selfHandle : SecureHandle_zero();
}

;;SETTER
static fn Graphics_setSelfHandle(Graphics *g, SecureHandle h) {
    if (g)
        (*g).selfHandle = h;
}

;;GETTER
static SecureHandle Graphics_getCmdHandle(const Graphics *g) {
    return g ? (*g).cmdHandle : SecureHandle_zero();
}

;;SETTER
static fn Graphics_setCmdHandle(Graphics *g, SecureHandle h) {
    if (g)
        (*g).cmdHandle = h;
}

/** ---------------------------------------------------------------- Built-in UMA/Software reference driver */

static bool swExecClear(const VideoPool *pool, GraphicsCommand cmd) {
    GraphicsImage dst = cmd.target;
    if (!GraphicsImage_isValid(&dst))
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

    u8 px[4] = {
        ColorRGBA_red(cmd.clearColorRGBA),
        ColorRGBA_green(cmd.clearColorRGBA),
        ColorRGBA_blue(cmd.clearColorRGBA),
        ColorRGBA_alpha(cmd.clearColorRGBA),
    };
    u8 *base = (u8*) VideoMemory_map(pool, dst.data);
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

static bool swExecCopy(const VideoPool *pool, GraphicsCommand cmd) {
    GraphicsImage dst = cmd.target;
    GraphicsImage src = cmd.source;
    if (!GraphicsImage_isValid(&dst) || !GraphicsImage_isValid(&src))
        return false;
    if (dst.format != src.format)
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

    u32 copyCols = GraphicsRect_width(clip);
    bytes rowBytes = (bytes) copyCols * (bytes) bpp;
    i32 srcStartX = clip.x0 - dstX;
    i32 srcStartY = clip.y0 - dstY;

    u8 *dstBase = (u8*) VideoMemory_map(pool, dst.data);
    u8 *srcBase = (u8*) VideoMemory_map(pool, src.data);
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
            vpointer sAddr = src.data + (u64) sy * (u64) src.strideBytes + (u64) srcStartX * (u64) bpp + done;
            vpointer dAddr = dst.data + (u64) y * (u64) dst.strideBytes + (u64) clip.x0 * (u64) bpp + done;
            if (!VideoMemory_read(pool, chunk, sAddr, step) ||
                !VideoMemory_write(pool, dAddr, chunk, step)) {
                return false;
            }
            done += step;
        }
    }
    return true;
}

static bool swSubmitCommands([[maybe_unused]] any context, const VideoPool *pool,
                             const GraphicsCommand *cmds, u32 count) {
    if (!pool)
        return false;
    for (u32 i = 0u; i < count; i++) {
        GraphicsCommand c = cmds[i];
        switch (c.kind) {
            case GRAPHICS_CMD_CLEAR:
                if (!swExecClear(pool, c))
                    return false;
                break;
            case GRAPHICS_CMD_COPY:
                if (!swExecCopy(pool, c))
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

static bool swPresentImage([[maybe_unused]] any context, const VideoPool *pool,
                           [[maybe_unused]] any nativeSurface, const GraphicsImage *image) {
    return pool != nullptr && GraphicsImage_isValid(image);
}

static GraphicsDriver swDefaultDriver()
{
    GraphicsDriver d;
    memset(&d, 0, sizeof(d));
    d.name = "software-uma";
    d.context = nullptr;
    d.registerShader = nullptr;
    d.submitCommands = swSubmitCommands;
    d.presentImage = swPresentImage;
    d.destroyContext = nullptr;
    return d;
}

/** ---------------------------------------------------------------- Graphics lifecycle */

static GraphicsCommand *Graphics_initCommandBuffer(u32 cap, SecureHandle *outHandle) {
    SecureMemoryArena *sa = Graphvex_secureArena();
    SecureHandle h = SecureMemoryArena_alloc(sa, 0u, (bytes) cap * sizeof(GraphicsCommand));
    if (!SecureHandle_isValid(h))
        return nullptr;
    if (outHandle)
        *outHandle = h;
    return (GraphicsCommand*) SecureMemoryArena_resolve(sa, h);
}

;;DECONSTRUCT
static fn Graphics_commands_destroy(SecureHandle cmdHandle) {
    if (SecureHandle_isValid(cmdHandle))
        (discard) SecureMemoryArena_free(Graphvex_secureArena(), cmdHandle);
}

static Graphics *Graphics_init(VideoPool *pool, const GraphicsDriver *driver) {
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
    Graphics_setSelfHandle(g, selfHandle);

    SecureHandle cmdHandle = SecureHandle_zero();
    GraphicsCommand *cmds = Graphics_initCommandBuffer(GRAPHICS_DEFAULT_CMD_CAP, &cmdHandle);
    if (!cmds) {
        Graphics_destroy(g);
        return nullptr;
    }

    Graphics_setPool(g, pool);
    Graphics_setDriver(g, driver ? *driver : swDefaultDriver());
    Graphics_setTargetInternal(g, nullptr);
    Graphics_setScissorInternal(g, GraphicsRect_0());
    Graphics_setScissorActive(g, false);
    Graphics_setFrameIndex(g, 0u);
    Graphics_setBindlessNextSlot(g, 1u);
    Graphics_setShaderCount(g, 0u);
    Graphics_setCommands(g, cmds);
    Graphics_setCmdHandle(g, cmdHandle);
    Graphics_setCommandCount(g, 0u);
    Graphics_setCommandCap(g, GRAPHICS_DEFAULT_CMD_CAP);
    return g;
}

;;CONSTRUCTOR
/**
 * Creates a Graphics context backed by an explicit driver.
 * @param pool   backing VideoPool
 * @param driver explicit GraphicsDriver vtable (nullptr selects software)
 * @return newly allocated Graphics context, or nullptr on failure
 */
Graphics *Graphics_2(VideoPool *pool, const GraphicsDriver *driver)
{
    return Graphics_init(pool, driver);
}

#if defined(VEX_PLATFORM_APPLE)
Graphics *Graphics_metal(VideoPool *pool);

static bool isMetalPoolName(string name) {
    return name && name[0] == 'm' && name[1] == 'e' &&
           name[2] == 't' && name[3] == 'a' && name[4] == 'l' && name[5] == '\0';
}
#endif

;;CONSTRUCTOR
Graphics *Graphics_1(VideoPool *pool)
{
    Graphics_latchBackend();
#if defined(VEX_PLATFORM_APPLE)
    if (pool && isMetalPoolName(VideoPool_driverName(pool)))
        return Graphics_metal(pool);
#endif
    return Graphics_init(pool, nullptr);
}

;;DECONSTRUCT
fn Graphics_destroy(Graphics *g) {
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

VideoPool *Graphics_pool(const Graphics *g) {
    return Graphics_getPool(g);
}

string Graphics_driverName(const Graphics *g) {
    GraphicsDriver d = Graphics_getDriver(g);
    return d.name ? d.name : "unknown";
}

u64 Graphics_frameIndex(const Graphics *g) {
    return Graphics_getFrameIndex(g);
}

u32 Graphics_pendingCount(const Graphics *g) {
    return Graphics_getCommandCount(g);
}

u32 Graphics_bindlessCount(const Graphics *g) {
    u32 next = Graphics_getBindlessNextSlot(g);
    return next > 1u ? next - 1u : 0u;
}

/** ---------------------------------------------------------------- Images, zero-copy crop, explicit copy */

static u32 gfxAllocBindlessSlot(Graphics *g) {
    u32 slot = Graphics_getBindlessNextSlot(g);
    Graphics_setBindlessNextSlot(g, slot + 1u);
    return slot;
}

;;CONSTRUCTOR
/**
 * Constructs a zeroed invalid GraphicsImage value.
 * @return zeroed GraphicsImage
 */
GraphicsImage GraphicsImage_0()
{
    GraphicsImage empty = { 0 };
    return empty;
}

;;CONSTRUCTOR
/**
 * Allocates a buffer-backed GraphicsImage and assigns a bindless slot.
 * @param g      active Graphics context
 * @param arena  target VideoArena for pixel storage
 * @param width  image width in pixels
 * @param height image height in pixels
 * @param format pixel format
 * @return allocated GraphicsImage, or zeroed invalid image on failure
 */
GraphicsImage GraphicsImage_5(Graphics *g, VideoArena *arena,
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

    vpointer addr = VideoArena_alloc(arena, (bytes) total64, VIDEO_MEMORY_DEFAULT_ALIGN);
    if (addr == 0u)
        return empty;

    GraphicsImage img;
    img.data = addr;
    img.width = width;
    img.height = height;
    img.strideBytes = (u32) stride64;
    img.bindlessSlot = gfxAllocBindlessSlot(g);
    img.format = format;
    img.flags = GRAPHICS_IMAGE_NONE;
    return img;
}

/** Zero-copy sub-image view: advances vpointer and preserves parent strideBytes. */
GraphicsImage Graphics_crop(const GraphicsImage *src, i32 x0, i32 y0, i32 x1, i32 y1) {
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

    GraphicsImage view;
    view.data = base + (u64) clipped.y0 * (u64) stride + (u64) clipped.x0 * (u64) bpp;
    view.width = GraphicsRect_width(clipped);
    view.height = GraphicsRect_height(clipped);
    view.strideBytes = stride;
    view.bindlessSlot = GraphicsImage_bindlessSlot(src);
    view.format = fmt;
    view.flags = GraphicsImage_flags(src) | GRAPHICS_IMAGE_SUBVIEW;
    return view;
}

GraphicsImage Graphics_copy_3(Graphics *g, VideoArena *arena, const GraphicsImage *src) {
    GraphicsImage empty = GraphicsImage_0();
    if (!g || !arena || !GraphicsImage_isValid(src))
        return empty;
    GraphicsImage dst = GraphicsImage(
        g, arena,
        GraphicsImage_width(src),
        GraphicsImage_height(src),
        GraphicsImage_format(src));
    if (!GraphicsImage_isValid(&dst))
        return empty;

    GraphicsCommand cmd = { 0 };
    cmd.kind = GRAPHICS_CMD_COPY;
    cmd.target = dst;
    cmd.source = *src;
    cmd.scissor = GraphicsRect_0();
    cmd.dstX = 0;
    cmd.dstY = 0;
    if (!Graphics_command(g, &cmd))
        return empty;
    return dst;
}

GraphicsImage Graphics_copy_7(Graphics *g, VideoArena *arena, const GraphicsImage *src,
                              i32 x0, i32 y0, i32 x1, i32 y1) {
    GraphicsImage view = Graphics_crop(src, x0, y0, x1, y1);
    if (!GraphicsImage_isValid(&view))
        return view;
    return Graphics_copy_3(g, arena, &view);
}

bool Graphics_copyTo(Graphics *g, GraphicsImage *dst, i32 dstX, i32 dstY,
                     const GraphicsImage *src) {
    if (!g || !GraphicsImage_isValid(dst) || !GraphicsImage_isValid(src))
        return false;
    GraphicsCommand cmd = { 0 };
    cmd.kind = GRAPHICS_CMD_COPY;
    cmd.target = *dst;
    cmd.source = *src;
    cmd.scissor = Graphics_getScissorActive(g) ? Graphics_getScissor(g) : GraphicsRect_0();
    cmd.dstX = dstX;
    cmd.dstY = dstY;
    return Graphics_command(g, &cmd);
}

/** ---------------------------------------------------------------- Target & scissor state */

fn Graphics_setTarget(Graphics *g, GraphicsImage *target) {
    Graphics_setTargetInternal(g, target);
}

GraphicsImage *Graphics_target(const Graphics *g) {
    return Graphics_getTarget(g);
}

fn Graphics_setScissor(Graphics *g, i32 x0, i32 y0, i32 x1, i32 y1) {
    Graphics_setScissorInternal(g, GraphicsRect_4(x0, y0, x1, y1));
    Graphics_setScissorActive(g, true);
}

fn Graphics_resetScissor(Graphics *g) {
    Graphics_setScissorInternal(g, GraphicsRect_0());
    Graphics_setScissorActive(g, false);
}

GraphicsRect Graphics_scissor(const Graphics *g) {
    return Graphics_getScissor(g);
}

/** ---------------------------------------------------------------- Shader registration */

u32 Graphics_registerShader(Graphics *g, string name, u32 stage) {
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

u32 Graphics_registerCompute(Graphics *g, string path, string entryName) {
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

u32 Graphics_registerRender(Graphics *g, string path, string entryPair) {
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

/** ---------------------------------------------------------------- Command queue */

static bool gfxEnsureCommandCapacity(Graphics *g, u32 needed) {
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

bool Graphics_command(Graphics *g, const GraphicsCommand *cmd) {
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

bool Graphics_clear(Graphics *g, u32 colorRGBA) {
    GraphicsImage *target = Graphics_getTarget(g);
    if (!g || !GraphicsImage_isValid(target))
        return false;
    GraphicsCommand cmd = { 0 };
    cmd.kind = GRAPHICS_CMD_CLEAR;
    cmd.target = *target;
    cmd.scissor = Graphics_getScissorActive(g) ? Graphics_getScissor(g) : GraphicsRect_0();
    cmd.clearColorRGBA = colorRGBA;
    return Graphics_command(g, &cmd);
}

bool Graphics_dispatchWithSource(Graphics *g, u32 shaderId, const GraphicsImage *source,
                                 u32 gridX, u32 gridY, u32 gridZ,
                                 vpointer uniformAddr, bytes uniformBytes) {
    if (!g || shaderId == 0u || gridX == 0u || gridY == 0u || gridZ == 0u)
        return false;
    GraphicsCommand cmd = { 0 };
    cmd.kind = GRAPHICS_CMD_DISPATCH;
    cmd.shaderId = shaderId;
    GraphicsImage *target = Graphics_getTarget(g);
    if (target)
        cmd.target = *target;
    if (source)
        cmd.source = *source;
    cmd.scissor = Graphics_getScissorActive(g) ? Graphics_getScissor(g) : GraphicsRect_0();
    cmd.gridX = gridX;
    cmd.gridY = gridY;
    cmd.gridZ = gridZ;
    cmd.uniformAddr = uniformAddr;
    cmd.uniformBytes = uniformBytes;
    return Graphics_command(g, &cmd);
}

bool Graphics_dispatch(Graphics *g, u32 shaderId, u32 gridX, u32 gridY, u32 gridZ,
                       vpointer uniformAddr, bytes uniformBytes) {
    return Graphics_dispatchWithSource(g, shaderId, nullptr, gridX, gridY, gridZ,
                                       uniformAddr, uniformBytes);
}

bool Graphics_drawWithSource(Graphics *g, u32 shaderId, const GraphicsImage *source,
                             vpointer vertexAddr, vpointer indexAddr,
                             u32 count, vpointer uniformAddr, bytes uniformBytes) {
    if (!g || shaderId == 0u || vertexAddr == 0u || count == 0u)
        return false;
    GraphicsCommand cmd = { 0 };
    cmd.kind = GRAPHICS_CMD_DRAW;
    cmd.shaderId = shaderId;
    GraphicsImage *target = Graphics_getTarget(g);
    if (target)
        cmd.target = *target;
    if (source)
        cmd.source = *source;
    cmd.scissor = Graphics_getScissorActive(g) ? Graphics_getScissor(g) : GraphicsRect_0();
    cmd.vertexAddr = vertexAddr;
    cmd.indexAddr = indexAddr;
    cmd.drawCount = count;
    cmd.uniformAddr = uniformAddr;
    cmd.uniformBytes = uniformBytes;
    return Graphics_command(g, &cmd);
}

bool Graphics_draw(Graphics *g, u32 shaderId, vpointer vertexAddr, vpointer indexAddr,
                   u32 count, vpointer uniformAddr, bytes uniformBytes) {
    return Graphics_drawWithSource(g, shaderId, nullptr, vertexAddr, indexAddr,
                                   count, uniformAddr, uniformBytes);
}

bool Graphics_submit(Graphics *g) {
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

/**
 * Reports whether the active backend driver supports direct-to-surface
 * rendering without an intermediate VideoPool framebuffer.
 * @param g active Graphics context
 * @return true if Graphics_submitToSurface is supported
 */
bool Graphics_supportsDirectSurface(const Graphics *g) {
    if (!g)
        return false;
    return Graphics_getDriver(g).submitToSurface != nullptr;
}

/**
 * Submits all recorded commands directly to the native platform surface
 * drawable at the requested viewport and fixed backing surface dimensions.
 * @param g             active Graphics context
 * @param nativeSurface opaque platform surface handle
 * @param viewportW     active window viewport width in pixels
 * @param viewportH     active window viewport height in pixels
 * @param surfaceW      fixed backing drawable width in pixels
 * @param surfaceH      fixed backing drawable height in pixels
 * @param sync          true for synchronous transactional present
 * @return true on success, false on failure
 */
bool Graphics_submitToSurface(Graphics *g, any nativeSurface,
                              u32 viewportW, u32 viewportH,
                              u32 surfaceW, u32 surfaceH, bool sync) {
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

/**
 * Hints whether subsequent Graphics_present calls should commit synchronously
 * inside the platform compositor transaction (for live or programmatic resizes).
 * @param g    active Graphics context
 * @param sync true for synchronous transactional present, false for async
 */
fn Graphics_setSyncPresent(Graphics *g, bool sync) {
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

bool Graphics_present(Graphics *g, any nativeSurface, const GraphicsImage *image) {
    if (!g || !GraphicsImage_isValid(image))
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

/**
 * Presents image synchronously inside the platform compositor transaction and
 * restores asynchronous present mode afterward.
 * @param g             active Graphics context
 * @param nativeSurface opaque platform surface handle
 * @param image         rendered RGBA8 GraphicsImage
 * @return true on success, false on failure
 */
bool Graphics_presentSync(Graphics *g, any nativeSurface, const GraphicsImage *image) {
    if (!g)
        return false;
    Graphics_setSyncPresent(g, true);
    bool ok = Graphics_present(g, nativeSurface, image);
    Graphics_setSyncPresent(g, false);
    return ok;
}
