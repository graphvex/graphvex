#include "vex.h"
#include "vio/vio_internal.h"

;;DEFINITION
/** VideoMemory provides the CPU <-> GPU transfer and mapping bridge between
 * vpointer (64-bit GPU virtual address) and pointer / any (CPU address). On
 * UMA / VIDEO_MEMORY_SHARED slabs, mapping is O(1) zero-copy; on private VRAM,
 * read/write dispatch through the driver's bounded staging transfer path. */

;;OVERVIEW
/** CLASS: VideoMemory (vio/vmem.c)
 * FUNCTION REGISTRY: toPointer, map, fromPointer, write, read, contains, isUma.
 * CONTRACT: VideoMemory_toPointer returns 0 when the vpointer lives in private
 * non-host-visible VRAM or is outside the pool. Transfers validate bounds
 * strictly against the owning slab before touching CPU or GPU memory. */

pointer VideoMemory_toPointer(const VideoPool *pool, vpointer addr) {
    VideoSlabInfo info = {0};
    bytes offset = 0u;
    if (!VideoPool_findByGpu(pool, addr, 1u, &info, &offset))
        return (pointer) 0;
    if (info.cpuBase == 0u)
        return (pointer) 0;
    return info.cpuBase + (pointer) offset;
}

any VideoMemory_map(const VideoPool *pool, vpointer addr) {
    pointer cpu = VideoMemory_toPointer(pool, addr);
    return cpu == 0u ? nullptr : (any) cpu;
}

vpointer VideoMemory_fromPointer(const VideoPool *pool, pointer cpuAddr) {
    VideoSlabInfo info = {0};
    bytes offset = 0u;
    if (!VideoPool_findByCpu(pool, cpuAddr, &info, &offset))
        return 0u;
    return info.gpuBase + (vpointer) offset;
}

bool VideoMemory_write(const VideoPool *pool, vpointer dst, const_any src, bytes length) {
    if (pool == nullptr || dst == 0u || src == nullptr || length == 0u)
        return false;
    VideoSlabInfo info = {0};
    bytes offset = 0u;
    if (!VideoPool_findByGpu(pool, dst, length, &info, &offset))
        return false;
    if (info.cpuBase != 0u) {
        memcpy((u8*) (info.cpuBase + (pointer) offset), src, length);
        return true;
    }
    VideoPoolDriver driver = VideoPool_driver(pool);
    if (driver.copyHostToDevice == nullptr)
        return false;
    return driver.copyHostToDevice(driver.context, &info, offset, src, length);
}

bool VideoMemory_read(const VideoPool *pool, any dst, vpointer src, bytes length) {
    if (pool == nullptr || dst == nullptr || src == 0u || length == 0u)
        return false;
    VideoSlabInfo info = {0};
    bytes offset = 0u;
    if (!VideoPool_findByGpu(pool, src, length, &info, &offset))
        return false;
    if (info.cpuBase != 0u) {
        memcpy(dst, (const u8*) (info.cpuBase + (pointer) offset), length);
        return true;
    }
    VideoPoolDriver driver = VideoPool_driver(pool);
    if (driver.copyDeviceToHost == nullptr)
        return false;
    return driver.copyDeviceToHost(driver.context, dst, &info, offset, length);
}

bool VideoMemory_contains(const VideoPool *pool, vpointer addr, bytes length) {
    if (length == 0u)
        length = 1u;
    return VideoPool_findByGpu(pool, addr, length, nullptr, nullptr);
}

bool VideoMemory_isUma(const VideoPool *pool) {
    return VideoPool_isUma(pool);
}
