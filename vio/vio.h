#ifndef GRAPHVEX_VIO_H
#define GRAPHVEX_VIO_H

/**
 * vio/vio.h -- GPU image memory substrate for graphvex (parallel to re/nio/).
 *
 * Three-tier GPU image memory ownership:
 * ImagePool  (growable raw GPU slabs from Metal/Vulkan/D3D12/wgpu)
 * ImageArena (per-subsystem virtual GPU arena carved from an ImagePool)
 * vpointer   (64-bit GPU virtual address, 0 = null)
 *
 * On Unified Memory (Apple Silicon / UMA), IMAGE_MEMORY_SHARED slabs are backed
 * by 16 KiB page-aligned host memory wrapped zero-copy into the GPU address
 * space, so ImageMemory_toPointer(pool, vptr) returns the direct CPU pointer in
 * O(1) without staging or copying.
 */

#include "vex.h"

#define IMAGE_MEMORY_ALIGN_FLOOR        32u
#define IMAGE_MEMORY_DEFAULT_ALIGN      256u
#define IMAGE_MEMORY_DEFAULT_SLAB_BYTES (1024u * 1024u)
#define IMAGE_MEMORY_WAIT_TIMEOUT_NS    (100u * 1000u * 1000u)

;;CLASSNAME("ImageMemoryDomain")

typedef enum ImageMemoryDomain {
    IMAGE_MEMORY_SHARED  = 0,
    IMAGE_MEMORY_PRIVATE = 1,
    IMAGE_MEMORY_STAGING = 2,
} ImageMemoryDomain;

;;CLASSNAME("ImageSlabInfo")
/**
 * Backing slab descriptor returned by ImagePoolDriver.allocSlab.
 *
 * vpointer gpuBase         - 64-bit GPU virtual base address
 * pointer cpuBase          - direct CPU base pointer (0 when device-private)
 * bytes length             - slab byte length
 * any driverHandle         - opaque backend slab handle
 * ImageMemoryDomain domain - memory residency domain
 * bool umaZeroCopy         - true when host and GPU share physical pages
 */
typedef struct ImageSlabInfo {
    vpointer gpuBase;
    pointer cpuBase;
    bytes length;
    any driverHandle;
    ImageMemoryDomain domain;
    bool umaZeroCopy;
} ImageSlabInfo;

;;CLASSNAME("ImagePoolDriver")
/**
 * Thin driver seam implemented per graphics API (metal/, vulkan/, direct/, wgpu/).
 * Public consumers never touch driver handles; drivers register an ImagePoolDriver.
 *
 * string name           - driver identifier ("metal", "software-uma", etc.)
 * any context           - opaque driver-owned state
 * bool uma              - true on unified-memory devices
 * fn allocSlab          - allocates one page-aligned GPU slab
 * fn freeSlab           - releases one GPU slab
 * fn copyHostToDevice   - stages bytes into private VRAM
 * fn copyDeviceToHost   - reads bytes back from private VRAM
 * fn destroyContext     - tears down driver context
 */
typedef struct ImagePoolDriver {
    string name;
    any context;
    bool uma;
    bool (*allocSlab)(any context, ImageMemoryDomain domain, bytes length, ImageSlabInfo *outSlab);
    fn (*freeSlab)(any context, const ImageSlabInfo *slab);
    bool (*copyHostToDevice)(any context, const ImageSlabInfo *slab, bytes offset, const_any src, bytes length);
    bool (*copyDeviceToHost)(any context, any dst, const ImageSlabInfo *slab, bytes offset, bytes length);
    fn (*destroyContext)(any context);
} ImagePoolDriver;

typedef struct ImagePool ImagePool;
typedef struct ImageArena ImageArena;

ImagePool *ImagePool_1(bytes slabBytes);
ImagePool *ImagePool_2(const ImagePoolDriver *driver, bytes slabBytes);
#define ImagePool(...) CONSTRUCTOR_DISPATCH(ImagePool, __VA_ARGS__)
ImagePool *ImagePool_software(bytes slabBytes);
fn ImagePool_destroy(ImagePool *pool);

bool ImagePool_isUma(const ImagePool *pool);
string ImagePool_driverName(const ImagePool *pool);
bytes ImagePool_slabBytes(const ImagePool *pool);
bytes ImagePool_slabCount(const ImagePool *pool);
bytes ImagePool_totalBytes(const ImagePool *pool);
bytes ImagePool_usedBytes(const ImagePool *pool);
u64 ImagePool_exhaustionCount(const ImagePool *pool);

ImageArena *ImageArena_3(ImagePool *pool, ImageMemoryDomain domain, bytes maxBytes);
#define ImageArena(...) CONSTRUCTOR_DISPATCH(ImageArena, __VA_ARGS__)
fn ImageArena_destroy(ImageArena *arena);

vpointer ImageArena_alloc(ImageArena *arena, bytes numBytes, bytes alignment);
fn ImageArena_free(ImageArena *arena, vpointer addr);
fn ImageArena_reset(ImageArena *arena);
fn ImageArena_trim(ImageArena *arena);

ImagePool *ImageArena_pool(const ImageArena *arena);
ImageMemoryDomain ImageArena_domain(const ImageArena *arena);
bytes ImageArena_usedBytes(const ImageArena *arena);
bytes ImageArena_capacity(const ImageArena *arena);
bytes ImageArena_maxBytes(const ImageArena *arena);
u64 ImageArena_exhaustionCount(const ImageArena *arena);

pointer ImageMemory_toPointer(const ImagePool *pool, vpointer addr);
any ImageMemory_map(const ImagePool *pool, vpointer addr);
vpointer ImageMemory_fromPointer(const ImagePool *pool, pointer cpuAddr);

bool ImageMemory_write(const ImagePool *pool, vpointer dst, const_any src, bytes length);
bool ImageMemory_read(const ImagePool *pool, any dst, vpointer src, bytes length);
bool ImageMemory_contains(const ImagePool *pool, vpointer addr, bytes length);
bool ImageMemory_isUma(const ImagePool *pool);

#if defined(VEX_PLATFORM_APPLE)
ImagePool *ImagePool_metal(bytes slabBytes);
#endif

#endif
