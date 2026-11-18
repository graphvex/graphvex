#ifndef GRAPHVEX_CORE_H
#define GRAPHVEX_CORE_H

// core/core.h — backend-agnostic GPU object vocabulary for graphvex.
//
// Directory Header Law: all public classes of `core/` live in this header,
// separated by ;;CLASSNAME annotations. Vendor headers (<vulkan/vulkan.h>,
// <Metal/Metal.h>, <d3d12.h>, <webgpu.h>) are strictly forbidden here.

#include "vex.h"

// ---------------------------------------------------------------- constants & type IDs

// Bounded Wait Law: 100 ms ceiling on every fence join and timeline wait.
#define FENCE_WAIT_TIMEOUT_NS 100'000'000ULL

// Strict 0xRRGGBBAA Color Law constants.
#define COLOR_CLEAR_RGBA 0x00000000u
#define COLOR_BLACK_RGBA 0x000000FFu
#define COLOR_WHITE_RGBA 0xFFFFFFFFu

// One Type Registry Law: graphvex api owns 0x0100 - 0x013F.
// (collage owns 0x0140 - 0x017F; meld owns 0x0180 - 0x01FF.)
#define ID_GRAPHICS_DEVICE 0x0100u
#define ID_CAPABILITY      0x0101u
#define ID_SURFACE         0x0102u
#define ID_SWAPCHAIN       0x0103u
#define ID_IMAGE           0x0104u
#define ID_IMAGE_VIEW      0x0105u
#define ID_SAMPLER         0x0106u
#define ID_GPU_BUFFER      0x0107u
#define ID_UPLOAD_RING     0x0108u
#define ID_BINDLESS_HEAP   0x0109u
#define ID_SHADER_MODULE   0x010Au
#define ID_PIPELINE_LAYOUT 0x010Bu
#define ID_PIPELINE        0x010Cu
#define ID_PASS            0x010Du
#define ID_BARRIER         0x010Eu
#define ID_COMMAND_BUFFER  0x010Fu
#define ID_COMMAND_QUEUE   0x0110u
#define ID_FENCE           0x0111u
#define ID_SEMAPHORE       0x0112u
#define ID_GRAPHICS_FRAME  0x0113u
#define ID_RETIRE_RING     0x0114u

// ---------------------------------------------------------------- BackendKind

;;CLASSNAME("BackendKind")

typedef enum BackendKind {
    BACKEND_VULKAN = 0,
    BACKEND_METAL  = 1,
    BACKEND_DIRECT = 2,
    BACKEND_WGPU   = 3,
    BACKEND_COUNT  = 4
} BackendKind;

const char *BackendKind_name(BackendKind kind);
bool BackendKind_isValid(u32 kind);

// ---------------------------------------------------------------- Capability

;;CLASSNAME("Capability")

typedef enum CapabilityBit {
    GRAPHVEX_CAP_NONE           = 0u,
    GRAPHVEX_CAP_BINDLESS       = 1u << 0,
    GRAPHVEX_CAP_TIMELINE       = 1u << 1,
    GRAPHVEX_CAP_MESH_SHADER    = 1u << 2,
    GRAPHVEX_CAP_RAY_QUERY      = 1u << 3,
    GRAPHVEX_CAP_RAY_PIPELINE   = 1u << 4,
    GRAPHVEX_CAP_STENCIL8       = 1u << 5,
    GRAPHVEX_CAP_F16_RENDER     = 1u << 6,
    GRAPHVEX_CAP_STORAGE_RGBA16 = 1u << 7,
    GRAPHVEX_CAP_INDIRECT_COUNT = 1u << 8,
    GRAPHVEX_CAP_BINDING_ARRAY  = 1u << 9
} CapabilityBit;

// ---------------------------------------------------------------- PixelFormat

;;CLASSNAME("PixelFormat")

typedef enum PixelFormat {
    PIXEL_FORMAT_RGBA8_UNORM     = 0,
    PIXEL_FORMAT_RGBA8_SRGB      = 1,
    PIXEL_FORMAT_RGBA16_FLOAT    = 2,
    PIXEL_FORMAT_RGBA32_FLOAT    = 3,
    PIXEL_FORMAT_RG16_FLOAT      = 4,
    PIXEL_FORMAT_R16_UNORM       = 5,
    PIXEL_FORMAT_R32_FLOAT       = 6,
    PIXEL_FORMAT_R32_UINT        = 7,
    PIXEL_FORMAT_D32_FLOAT       = 8,
    PIXEL_FORMAT_D32_FLOAT_S8    = 9,
    PIXEL_FORMAT_COUNT           = 10
} PixelFormat;

u32 PixelFormat_bytesPerPixel(PixelFormat format);

// ---------------------------------------------------------------- ColorRGBA

;;CLASSNAME("ColorRGBA")

static inline u8 ColorRGBA_red(u32 rgba)   { return (u8) ((rgba >> 24) & 0xFFu); }
static inline u8 ColorRGBA_green(u32 rgba) { return (u8) ((rgba >> 16) & 0xFFu); }
static inline u8 ColorRGBA_blue(u32 rgba)  { return (u8) ((rgba >> 8)  & 0xFFu); }
static inline u8 ColorRGBA_alpha(u32 rgba) { return (u8) (rgba & 0xFFu); }
static inline u32 ColorRGBA_pack(u8 r, u8 g, u8 b, u8 a) {
    return ((u32) r << 24) | ((u32) g << 16) | ((u32) b << 8) | (u32) a;
}

// ---------------------------------------------------------------- GraphicsDrawable

;;CLASSNAME("GraphicsDrawable")

typedef struct GraphicsDrawable {
    any handle;
    u32 width;
    u32 height;
    f32 scale;
    u32 _pad[3];
} GraphicsDrawable;

_Static_assert(sizeof(GraphicsDrawable) == 32, "GraphicsDrawable obeys 32-byte floor");

;;GETTER
any GraphicsDrawable_handle(const GraphicsDrawable *self);
;;SETTER
fn GraphicsDrawable_setHandle(GraphicsDrawable *self, any handle);

;;GETTER
u32 GraphicsDrawable_width(const GraphicsDrawable *self);
;;SETTER
fn GraphicsDrawable_setWidth(GraphicsDrawable *self, u32 width);

;;GETTER
u32 GraphicsDrawable_height(const GraphicsDrawable *self);
;;SETTER
fn GraphicsDrawable_setHeight(GraphicsDrawable *self, u32 height);

;;GETTER
f32 GraphicsDrawable_scale(const GraphicsDrawable *self);
;;SETTER
fn GraphicsDrawable_setScale(GraphicsDrawable *self, f32 scale);

// ---------------------------------------------------------------- GraphicsDevice

;;CLASSNAME("GraphicsDevice")

typedef struct GraphicsDevice {
    BackendKind backend;
    u32 capabilities;
    u64 frameIndex;
    any driverHandle;
    bool initialized;
    u8 _pad[7];
} GraphicsDevice;

_Static_assert(sizeof(GraphicsDevice) == 32, "GraphicsDevice obeys 32-byte floor");

GraphicsDevice GraphicsDevice_0();
GraphicsDevice GraphicsDevice_1(BackendKind backend);
#define GraphicsDevice(...) CONSTRUCTOR_DISPATCH(GraphicsDevice, __VA_ARGS__)

;;GETTER
BackendKind GraphicsDevice_backend(const GraphicsDevice *self);
;;SETTER
fn GraphicsDevice_setBackend(GraphicsDevice *self, BackendKind backend);

;;GETTER
u32 GraphicsDevice_capabilities(const GraphicsDevice *self);
;;SETTER
fn GraphicsDevice_setCapabilities(GraphicsDevice *self, u32 capabilities);

;;GETTER
u64 GraphicsDevice_frameIndex(const GraphicsDevice *self);
;;SETTER
fn GraphicsDevice_setFrameIndex(GraphicsDevice *self, u64 frameIndex);

bool GraphicsDevice_supports(const GraphicsDevice *self, u32 capabilityMask);

// ---------------------------------------------------------------- Image

;;CLASSNAME("Image")

typedef struct Image {
    u32 width;
    u32 height;
    PixelFormat format;
    u32 bindlessSlot;
    u8 *shadowPixels;
    any driverHandle;
} Image;

_Static_assert(sizeof(Image) == 32, "Image obeys 32-byte floor");

Image Image_0();
Image Image_2(u32 width, u32 height);
Image Image_3(u32 width, u32 height, PixelFormat format);
#define Image(...) CONSTRUCTOR_DISPATCH(Image, __VA_ARGS__)

;;GETTER
u32 Image_width(const Image *self);
;;SETTER
fn Image_setWidth(Image *self, u32 width);

;;GETTER
u32 Image_height(const Image *self);
;;SETTER
fn Image_setHeight(Image *self, u32 height);

;;GETTER
PixelFormat Image_format(const Image *self);
;;SETTER
fn Image_setFormat(Image *self, PixelFormat format);

;;GETTER
u32 Image_bindlessSlot(const Image *self);
;;SETTER
fn Image_setBindlessSlot(Image *self, u32 slot);

;;GETTER
u8 *Image_shadowPixels(const Image *self);
;;SETTER
fn Image_setShadowPixels(Image *self, u8 *pixels);

fn Image_fitRect(const Image *self, f32 dstX, f32 dstY, f32 dstW, f32 dstH,
                 f32 *outX, f32 *outY, f32 *outW, f32 *outH);

// ---------------------------------------------------------------- GpuBuffer

;;CLASSNAME("GpuBuffer")

typedef enum GpuBufferUsage {
    GPU_BUFFER_VERTEX   = 1u << 0,
    GPU_BUFFER_INDEX    = 1u << 1,
    GPU_BUFFER_UNIFORM  = 1u << 2,
    GPU_BUFFER_STORAGE  = 1u << 3,
    GPU_BUFFER_INDIRECT = 1u << 4,
    GPU_BUFFER_STAGING  = 1u << 5
} GpuBufferUsage;

typedef struct GpuBuffer {
    bytes capacity;
    u32 usage;
    u32 stride;
    u8 *shadowBytes;
    any driverHandle;
} GpuBuffer;

_Static_assert(sizeof(GpuBuffer) == 32, "GpuBuffer obeys 32-byte floor");

GpuBuffer GpuBuffer_0();
GpuBuffer GpuBuffer_2(bytes capacity, u32 usage);
GpuBuffer GpuBuffer_3(bytes capacity, u32 usage, u32 stride);
#define GpuBuffer(...) CONSTRUCTOR_DISPATCH(GpuBuffer, __VA_ARGS__)

;;GETTER
bytes GpuBuffer_capacity(const GpuBuffer *self);
;;SETTER
fn GpuBuffer_setCapacity(GpuBuffer *self, bytes capacity);

;;GETTER
u32 GpuBuffer_usage(const GpuBuffer *self);
;;SETTER
fn GpuBuffer_setUsage(GpuBuffer *self, u32 usage);

;;GETTER
u32 GpuBuffer_stride(const GpuBuffer *self);
;;SETTER
fn GpuBuffer_setStride(GpuBuffer *self, u32 stride);

// ---------------------------------------------------------------- Fence & Semaphore

;;CLASSNAME("Fence")

typedef struct Fence {
    u64 targetValue;
    u64 completedValue;
    any driverHandle;
    bool signaled;
    u8 _pad[7];
} Fence;

_Static_assert(sizeof(Fence) == 32, "Fence obeys 32-byte floor");

Fence Fence_0();
#define Fence(...) CONSTRUCTOR_DISPATCH(Fence, __VA_ARGS__)

;;GETTER
bool Fence_signaled(const Fence *self);
;;SETTER
fn Fence_setSignaled(Fence *self, bool signaled);

bool Fence_waitBounded(Fence *self, u64 timeoutNs);

;;CLASSNAME("Semaphore")

typedef struct Semaphore {
    u64 timelineValue;
    any driverHandle;
    u64 _pad[2];
} Semaphore;

_Static_assert(sizeof(Semaphore) == 32, "Semaphore obeys 32-byte floor");

Semaphore Semaphore_0();
#define Semaphore(...) CONSTRUCTOR_DISPATCH(Semaphore, __VA_ARGS__)

;;GETTER
u64 Semaphore_timelineValue(const Semaphore *self);
;;SETTER
fn Semaphore_setTimelineValue(Semaphore *self, u64 value);

u64 Semaphore_signalNext(Semaphore *self);

// ---------------------------------------------------------------- BindlessHeap

;;CLASSNAME("BindlessHeap")

#define BINDLESS_DUMMY_WHITE_SLOT 0u
#define BINDLESS_DEFAULT_CAPACITY 1024u

typedef struct BindlessHeap {
    u32 capacity;
    u32 count;
    any driverHandle;
    u64 _pad[2];
} BindlessHeap;

_Static_assert(sizeof(BindlessHeap) == 32, "BindlessHeap obeys 32-byte floor");

BindlessHeap BindlessHeap_0();
BindlessHeap BindlessHeap_1(u32 capacity);
#define BindlessHeap(...) CONSTRUCTOR_DISPATCH(BindlessHeap, __VA_ARGS__)

;;GETTER
u32 BindlessHeap_capacity(const BindlessHeap *self);
;;GETTER
u32 BindlessHeap_count(const BindlessHeap *self);

u32 BindlessHeap_registerImage(BindlessHeap *self, Image *image);

// ---------------------------------------------------------------- Swapchain, CommandQueue, CommandBuffer, Pass, Pipeline

;;CLASSNAME("Swapchain")

typedef struct Swapchain {
    u32 width;
    u32 height;
    u32 imageCount;
    u32 currentIndex;
    any driverHandle;
    u64 _pad;
} Swapchain;

_Static_assert(sizeof(Swapchain) == 32, "Swapchain obeys 32-byte floor");

;;CLASSNAME("CommandQueue")

typedef enum QueueKind {
    QUEUE_GRAPHICS = 0,
    QUEUE_COMPUTE  = 1,
    QUEUE_COPY     = 2
} QueueKind;

typedef struct CommandQueue {
    QueueKind kind;
    u32 familyIndex;
    any driverHandle;
    u64 _pad[2];
} CommandQueue;

_Static_assert(sizeof(CommandQueue) == 32, "CommandQueue obeys 32-byte floor");

;;CLASSNAME("CommandBuffer")

typedef struct CommandBuffer {
    QueueKind queueKind;
    bool recording;
    u8 _pad0[3];
    any driverHandle;
    u64 _pad[2];
} CommandBuffer;

_Static_assert(sizeof(CommandBuffer) == 32, "CommandBuffer obeys 32-byte floor");

;;CLASSNAME("Pipeline")

typedef struct Pipeline {
    u32 shaderModuleId;
    PixelFormat colorFormat;
    PixelFormat depthFormat;
    u32 sampleCount;
    any driverHandle;
    u64 _pad;
} Pipeline;

_Static_assert(sizeof(Pipeline) == 32, "Pipeline obeys 32-byte floor");

;;CLASSNAME("Pass")

typedef struct Pass {
    u32 width;
    u32 height;
    u32 clearColorRGBA;
    f32 clearDepth;
    any driverHandle;
    u64 _pad;
} Pass;

_Static_assert(sizeof(Pass) == 32, "Pass obeys 32-byte floor");

#endif
