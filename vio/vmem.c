#include "vex.h"
#include "vio/vio_internal.h"

#include <string.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ImageMemory
 * ============================================================================
 * Provides the CPU to GPU transfer and mapping bridge between vpointer
 * (64-bit GPU virtual address) and pointer / any (CPU address). On UMA and
 * IMAGE_MEMORY_SHARED slabs, mapping is O(1) zero-copy; on private VRAM,
 * read/write dispatch through the driver's bounded staging transfer path.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ImageMemory (vio/vmem.c)
 * ============================================================================
 * Host-to-device mapping and bounded transfer operations over ImagePool.
 *
 * --FUNCTIONS----------------------------
 * public   ImageMemory_toPointer/map/fromPointer  zero-copy UMA address bridge
 * public   ImageMemory_write/read                 domain-aware byte transfers
 * public   ImageMemory_contains/isUma             pool membership queries
 * ============================================================================
 */

pointer ImageMemory_toPointer(const ImagePool *pool, vpointer addr)
{
    ImageSlabInfo info;
    memset(&info, 0, sizeof(ImageSlabInfo));
    bytes offset = 0u;
    if (!ImagePool_findByGpu(pool, addr, 1u, &info, &offset))
        return (pointer) 0;

    if (info.cpuBase == 0u)
        return (pointer) 0;

    return info.cpuBase + (pointer) offset;
}

any ImageMemory_map(const ImagePool *pool, vpointer addr)
{
    pointer cpu = ImageMemory_toPointer(pool, addr);
    if (cpu == 0u)
        return nullptr;

    return (any) cpu;
}

vpointer ImageMemory_fromPointer(const ImagePool *pool, pointer cpuAddr)
{
    ImageSlabInfo info;
    memset(&info, 0, sizeof(ImageSlabInfo));
    bytes offset = 0u;
    if (!ImagePool_findByCpu(pool, cpuAddr, &info, &offset))
        return 0u;

    return info.gpuBase + (vpointer) offset;
}

bool ImageMemory_write(const ImagePool *pool, vpointer dst, const_any src, bytes length)
{
    if (pool == nullptr || dst == 0u || src == nullptr || length == 0u)
        return false;

    ImageSlabInfo info;
    memset(&info, 0, sizeof(ImageSlabInfo));
    bytes offset = 0u;
    if (!ImagePool_findByGpu(pool, dst, length, &info, &offset))
        return false;

    if (info.cpuBase != 0u) {
        memcpy((u8*) (info.cpuBase + (pointer) offset), src, length);
        return true;
    }
    ImagePoolDriver driver = ImagePool_driver(pool);
    if (driver.copyHostToDevice == nullptr)
        return false;

    return driver.copyHostToDevice(driver.context, &info, offset, src, length);
}

bool ImageMemory_read(const ImagePool *pool, any dst, vpointer src, bytes length)
{
    if (pool == nullptr || dst == nullptr || src == 0u || length == 0u)
        return false;

    ImageSlabInfo info;
    memset(&info, 0, sizeof(ImageSlabInfo));
    bytes offset = 0u;
    if (!ImagePool_findByGpu(pool, src, length, &info, &offset))
        return false;

    if (info.cpuBase != 0u) {
        memcpy(dst, (const u8*) (info.cpuBase + (pointer) offset), length);
        return true;
    }
    ImagePoolDriver driver = ImagePool_driver(pool);
    if (driver.copyDeviceToHost == nullptr)
        return false;

    return driver.copyDeviceToHost(driver.context, dst, &info, offset, length);
}

bool ImageMemory_contains(const ImagePool *pool, vpointer addr, bytes length)
{
    if (length == 0u)
        length = 1u;

    return ImagePool_findByGpu(pool, addr, length, nullptr, nullptr);
}

bool ImageMemory_isUma(const ImagePool *pool)
{
    return ImagePool_isUma(pool);
}
