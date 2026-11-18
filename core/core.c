// core/core.c — backend-agnostic GPU primitives and CPU-shadow state.

#include "vex.h"
#include "core/core.h"

;;OVERVIEW
;;DEFINITION

// ---------------------------------------------------------------- BackendKind & PixelFormat

const char *BackendKind_name(BackendKind kind) {
    switch (kind) {
        case BACKEND_VULKAN: return "VULKAN";
        case BACKEND_METAL:  return "METAL";
        case BACKEND_DIRECT: return "DIRECT";
        case BACKEND_WGPU:   return "WGPU";
        default:             return "UNKNOWN";
    }
}

bool BackendKind_isValid(u32 kind) {
    return kind < BACKEND_COUNT;
}

u32 PixelFormat_bytesPerPixel(PixelFormat format) {
    switch (format) {
        case PIXEL_FORMAT_RGBA8_UNORM:
        case PIXEL_FORMAT_RGBA8_SRGB:
        case PIXEL_FORMAT_RG16_FLOAT:
        case PIXEL_FORMAT_R32_FLOAT:
        case PIXEL_FORMAT_R32_UINT:
        case PIXEL_FORMAT_D32_FLOAT:    return 4u;
        case PIXEL_FORMAT_R16_UNORM:    return 2u;
        case PIXEL_FORMAT_RGBA16_FLOAT:
        case PIXEL_FORMAT_D32_FLOAT_S8: return 8u;
        case PIXEL_FORMAT_RGBA32_FLOAT: return 16u;
        default:                        return 4u;
    }
}

// ---------------------------------------------------------------- GraphicsDrawable

;;GETTER
any GraphicsDrawable_handle(const GraphicsDrawable *self) {
    return self ? (*self).handle : nullptr;
}
;;SETTER
fn GraphicsDrawable_setHandle(GraphicsDrawable *self, any handle) {
    if (self) (*self).handle = handle;
}

;;GETTER
u32 GraphicsDrawable_width(const GraphicsDrawable *self) {
    return self ? (*self).width : 0u;
}
;;SETTER
fn GraphicsDrawable_setWidth(GraphicsDrawable *self, u32 width) {
    if (self) (*self).width = width;
}

;;GETTER
u32 GraphicsDrawable_height(const GraphicsDrawable *self) {
    return self ? (*self).height : 0u;
}
;;SETTER
fn GraphicsDrawable_setHeight(GraphicsDrawable *self, u32 height) {
    if (self) (*self).height = height;
}

;;GETTER
f32 GraphicsDrawable_scale(const GraphicsDrawable *self) {
    return self ? (*self).scale : 1.0f;
}
;;SETTER
fn GraphicsDrawable_setScale(GraphicsDrawable *self, f32 scale) {
    if (self) (*self).scale = scale;
}

// ---------------------------------------------------------------- GraphicsDevice

static u32 defaultCapabilitiesForBackend(BackendKind backend) {
    switch (backend) {
        case BACKEND_VULKAN:
            return GRAPHVEX_CAP_BINDLESS | GRAPHVEX_CAP_TIMELINE |
                   GRAPHVEX_CAP_STENCIL8 | GRAPHVEX_CAP_F16_RENDER |
                   GRAPHVEX_CAP_STORAGE_RGBA16 | GRAPHVEX_CAP_INDIRECT_COUNT;
        case BACKEND_METAL:
            return GRAPHVEX_CAP_BINDLESS | GRAPHVEX_CAP_TIMELINE |
                   GRAPHVEX_CAP_MESH_SHADER | GRAPHVEX_CAP_RAY_QUERY |
                   GRAPHVEX_CAP_STENCIL8 | GRAPHVEX_CAP_F16_RENDER |
                   GRAPHVEX_CAP_STORAGE_RGBA16 | GRAPHVEX_CAP_INDIRECT_COUNT;
        case BACKEND_DIRECT:
            return GRAPHVEX_CAP_BINDLESS | GRAPHVEX_CAP_TIMELINE |
                   GRAPHVEX_CAP_MESH_SHADER | GRAPHVEX_CAP_RAY_QUERY |
                   GRAPHVEX_CAP_RAY_PIPELINE | GRAPHVEX_CAP_STENCIL8 |
                   GRAPHVEX_CAP_F16_RENDER | GRAPHVEX_CAP_STORAGE_RGBA16 |
                   GRAPHVEX_CAP_INDIRECT_COUNT;
        case BACKEND_WGPU:
            return GRAPHVEX_CAP_STENCIL8 | GRAPHVEX_CAP_F16_RENDER |
                   GRAPHVEX_CAP_STORAGE_RGBA16;
        default:
            return GRAPHVEX_CAP_NONE;
    }
}

GraphicsDevice GraphicsDevice_1(BackendKind backend) {
    GraphicsDevice dev = { 0 };
    if (!BackendKind_isValid((u32) backend)) {
        THROW("GraphicsDevice: invalid backend %u", (u32) backend);
        backend = BACKEND_VULKAN;
    }
    GraphicsDevice_setBackend(&dev, backend);
    GraphicsDevice_setCapabilities(&dev, defaultCapabilitiesForBackend(backend));
    GraphicsDevice_setFrameIndex(&dev, 0u);
    dev.initialized = true;
    return dev;
}

GraphicsDevice GraphicsDevice_0() {
    return GraphicsDevice_1(BACKEND_VULKAN);
}

;;GETTER
BackendKind GraphicsDevice_backend(const GraphicsDevice *self) {
    return self ? (*self).backend : BACKEND_VULKAN;
}
;;SETTER
fn GraphicsDevice_setBackend(GraphicsDevice *self, BackendKind backend) {
    if (self) (*self).backend = backend;
}

;;GETTER
u32 GraphicsDevice_capabilities(const GraphicsDevice *self) {
    return self ? (*self).capabilities : 0u;
}
;;SETTER
fn GraphicsDevice_setCapabilities(GraphicsDevice *self, u32 capabilities) {
    if (self) (*self).capabilities = capabilities;
}

;;GETTER
u64 GraphicsDevice_frameIndex(const GraphicsDevice *self) {
    return self ? (*self).frameIndex : 0u;
}
;;SETTER
fn GraphicsDevice_setFrameIndex(GraphicsDevice *self, u64 frameIndex) {
    if (self) (*self).frameIndex = frameIndex;
}

bool GraphicsDevice_supports(const GraphicsDevice *self, u32 capabilityMask) {
    if (!self) return false;
    return (GraphicsDevice_capabilities(self) & capabilityMask) == capabilityMask;
}

// ---------------------------------------------------------------- Image

Image Image_3(u32 width, u32 height, PixelFormat format) {
    Image img = { 0 };
    Image_setWidth(&img, width);
    Image_setHeight(&img, height);
    Image_setFormat(&img, format);
    Image_setBindlessSlot(&img, BINDLESS_DUMMY_WHITE_SLOT);
    return img;
}

Image Image_2(u32 width, u32 height) {
    return Image_3(width, height, PIXEL_FORMAT_RGBA8_UNORM);
}

Image Image_0() {
    return Image_3(0u, 0u, PIXEL_FORMAT_RGBA8_UNORM);
}

;;GETTER
u32 Image_width(const Image *self) { return self ? (*self).width : 0u; }
;;SETTER
fn Image_setWidth(Image *self, u32 width) { if (self) (*self).width = width; }

;;GETTER
u32 Image_height(const Image *self) { return self ? (*self).height : 0u; }
;;SETTER
fn Image_setHeight(Image *self, u32 height) { if (self) (*self).height = height; }

;;GETTER
PixelFormat Image_format(const Image *self) {
    return self ? (*self).format : PIXEL_FORMAT_RGBA8_UNORM;
}
;;SETTER
fn Image_setFormat(Image *self, PixelFormat format) { if (self) (*self).format = format; }

;;GETTER
u32 Image_bindlessSlot(const Image *self) { return self ? (*self).bindlessSlot : 0u; }
;;SETTER
fn Image_setBindlessSlot(Image *self, u32 slot) { if (self) (*self).bindlessSlot = slot; }

;;GETTER
u8 *Image_shadowPixels(const Image *self) { return self ? (*self).shadowPixels : nullptr; }
;;SETTER
fn Image_setShadowPixels(Image *self, u8 *pixels) { if (self) (*self).shadowPixels = pixels; }

fn Image_fitRect(const Image *self, f32 dstX, f32 dstY, f32 dstW, f32 dstH,
                 f32 *outX, f32 *outY, f32 *outW, f32 *outH) {
    if (outX) *outX = dstX;
    if (outY) *outY = dstY;
    if (outW) *outW = 0.0f;
    if (outH) *outH = 0.0f;
    if (!self || dstW <= 0.0f || dstH <= 0.0f) return;
    u32 iw = Image_width(self), ih = Image_height(self);
    if (iw == 0u || ih == 0u) return;
    f32 scaleX = dstW / (f32) iw;
    f32 scaleY = dstH / (f32) ih;
    f32 scale = (scaleX < scaleY) ? scaleX : scaleY;
    f32 fitW = (f32) iw * scale;
    f32 fitH = (f32) ih * scale;
    if (outX) *outX = dstX + (dstW - fitW) * 0.5f;
    if (outY) *outY = dstY + (dstH - fitH) * 0.5f;
    if (outW) *outW = fitW;
    if (outH) *outH = fitH;
}

// ---------------------------------------------------------------- GpuBuffer

GpuBuffer GpuBuffer_3(bytes capacity, u32 usage, u32 stride) {
    GpuBuffer buf = { 0 };
    GpuBuffer_setCapacity(&buf, capacity);
    GpuBuffer_setUsage(&buf, usage);
    GpuBuffer_setStride(&buf, stride);
    return buf;
}

GpuBuffer GpuBuffer_2(bytes capacity, u32 usage) {
    return GpuBuffer_3(capacity, usage, 0u);
}

GpuBuffer GpuBuffer_0() {
    return GpuBuffer_3(0u, 0u, 0u);
}

;;GETTER
bytes GpuBuffer_capacity(const GpuBuffer *self) { return self ? (*self).capacity : 0u; }
;;SETTER
fn GpuBuffer_setCapacity(GpuBuffer *self, bytes capacity) { if (self) (*self).capacity = capacity; }

;;GETTER
u32 GpuBuffer_usage(const GpuBuffer *self) { return self ? (*self).usage : 0u; }
;;SETTER
fn GpuBuffer_setUsage(GpuBuffer *self, u32 usage) { if (self) (*self).usage = usage; }

;;GETTER
u32 GpuBuffer_stride(const GpuBuffer *self) { return self ? (*self).stride : 0u; }
;;SETTER
fn GpuBuffer_setStride(GpuBuffer *self, u32 stride) { if (self) (*self).stride = stride; }

// ---------------------------------------------------------------- Fence & Semaphore

Fence Fence_0() {
    Fence f = { 0 };
    Fence_setSignaled(&f, true);
    return f;
}

;;GETTER
bool Fence_signaled(const Fence *self) { return self ? (*self).signaled : false; }
;;SETTER
fn Fence_setSignaled(Fence *self, bool signaled) { if (self) (*self).signaled = signaled; }

bool Fence_waitBounded(Fence *self, u64 timeoutNs) {
    if (!self) return false;
    if (timeoutNs > FENCE_WAIT_TIMEOUT_NS) {
        THROW("Fence_waitBounded: timeout %llu exceeds FENCE_WAIT_TIMEOUT_NS (100ms)",
              (unsigned long long) timeoutNs);
        return false;
    }
    return Fence_signaled(self);
}

Semaphore Semaphore_0() {
    Semaphore s = { 0 };
    Semaphore_setTimelineValue(&s, 0u);
    return s;
}

;;GETTER
u64 Semaphore_timelineValue(const Semaphore *self) { return self ? (*self).timelineValue : 0u; }
;;SETTER
fn Semaphore_setTimelineValue(Semaphore *self, u64 value) { if (self) (*self).timelineValue = value; }

u64 Semaphore_signalNext(Semaphore *self) {
    if (!self) return 0u;
    u64 next = Semaphore_timelineValue(self) + 1u;
    Semaphore_setTimelineValue(self, next);
    return next;
}

// ---------------------------------------------------------------- BindlessHeap

BindlessHeap BindlessHeap_1(u32 capacity) {
    BindlessHeap heap = { 0 };
    if (capacity == 0u) capacity = BINDLESS_DEFAULT_CAPACITY;
    heap.capacity = capacity;
    // Slot 0 is reserved for the dummy white texture.
    heap.count = 1u;
    return heap;
}

BindlessHeap BindlessHeap_0() {
    return BindlessHeap_1(BINDLESS_DEFAULT_CAPACITY);
}

;;GETTER
u32 BindlessHeap_capacity(const BindlessHeap *self) { return self ? (*self).capacity : 0u; }
;;GETTER
u32 BindlessHeap_count(const BindlessHeap *self) { return self ? (*self).count : 0u; }
;;SETTER
static fn BindlessHeap_setCount(BindlessHeap *self, u32 count) { if (self) (*self).count = count; }

u32 BindlessHeap_registerImage(BindlessHeap *self, Image *image) {
    if (!self || !image) return BINDLESS_DUMMY_WHITE_SLOT;
    if (BindlessHeap_count(self) >= BindlessHeap_capacity(self)) {
        THROW("BindlessHeap: exhausted capacity %u", BindlessHeap_capacity(self));
        return BINDLESS_DUMMY_WHITE_SLOT;
    }
    u32 slot = BindlessHeap_count(self);
    BindlessHeap_setCount(self, slot + 1u);
    Image_setBindlessSlot(image, slot);
    return slot;
}
