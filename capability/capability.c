#include "vex.h"
#include "capability/capability.h"
#include "vulkan/device.h"

// following the directory going forward:
// worktree > organization > repository > <directory tree> > file

#include <string.h>
#include <vulkan/vulkan.h>

#if defined(VEX_PLATFORM_APPLE)
#include <sys/sysctl.h>
#endif

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GpuCapability
 * ============================================================================
 * R3 capability facts, answered on demand. There is deliberately no snapshot
 * and no probe entry point: every answer is derived from the live Device or
 * from a sysctl, so no caller can observe a half-initialized capability state
 * and no ordering rule exists. A query is pure; a fact does not transition.
 *
 * Device facts read the Vulkan physical device owned by vulkan/device.c and
 * answer at extension granularity (ray tracing = ray_tracing_pipeline or
 * ray_query; mesh shaders = VK_EXT_mesh_shader). A driver exposing more than
 * CAPABILITY_EXTENSION_CAP device extensions fails closed rather than
 * truncating the scan, because a wrong "supported" bit is worse than a
 * conservative "not supported".
 *
 * Host facts (chip, media engine) are co-located here for now: the R0/R1 host
 * capability class does not expose them yet. They read no device state and will
 * move down to lifetime when that face lands; nothing in this file depends on
 * order, so the move is a rename, not a rewrite.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GpuCapability (capability/capability.c)
 * ============================================================================
 * STRUCT FIELDS: none — procedural. No file-static mutable state exists, so
 * there is nothing to initialize, reset, or invalidate.
 *
 * FUNCTION REGISTRY
 * ----------------------------------------------------------------------------
 * Device facts:
 *   - GpuCapability_available(device)
 *   - GpuCapability_has(device, feature)
 *   - GpuCapability_family(device)
 * Host facts:
 *   - GpuCapability_chip()          // machdep.cpu.brand_string -> HostChip
 *   - GpuCapability_mediaEngine()   // chip-family inference, stated below
 * Names (cold path):
 *   - GpuCapability_featureName / familyName / chipName / mediaEngineName
 * Private helpers:
 *   - deviceExtension(device, name)
 *   - matchChip(brand)
 * ============================================================================
 */

#define CAPABILITY_EXTENSION_CAP 64u

// True when the physical device advertises this device extension. Fails closed
// when the driver reports more properties than the bounded scan holds.
static bool deviceExtension(const Device *device, const char *name) {
    if (device == nullptr || name == nullptr)
        return false;
    VkPhysicalDevice physical = (VkPhysicalDevice) Device_physical(device);
    if (physical == nullptr)
        return false;
    u32 total = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &total, nullptr) != VK_SUCCESS)
        return false;
    if (total == 0 || total > CAPABILITY_EXTENSION_CAP)
        return false;
    VkExtensionProperties props[CAPABILITY_EXTENSION_CAP];
    u32 count = total;
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, props) != VK_SUCCESS)
        return false;
    for (u32 i = 0; i < count; i++)
        if (strcmp(props[i].extensionName, name) == 0)
            return true;
    return false;
}

// Longest names first: "M1 Pro" must win over "M1".
static HostChip matchChip(const char *brand) {
    if (strstr(brand, "A18 Pro") != nullptr)   return HOST_CHIP_A18_PRO;
    if (strstr(brand, "A18") != nullptr)       return HOST_CHIP_A18;
    if (strstr(brand, "A17 Pro") != nullptr)   return HOST_CHIP_A17_PRO;
    if (strstr(brand, "M1 Ultra") != nullptr)  return HOST_CHIP_M1_ULTRA;
    if (strstr(brand, "M1 Max") != nullptr)    return HOST_CHIP_M1_MAX;
    if (strstr(brand, "M1 Pro") != nullptr)    return HOST_CHIP_M1_PRO;
    if (strstr(brand, "M1") != nullptr)        return HOST_CHIP_M1;
    if (strstr(brand, "M2 Ultra") != nullptr)  return HOST_CHIP_M2_ULTRA;
    if (strstr(brand, "M2 Max") != nullptr)    return HOST_CHIP_M2_MAX;
    if (strstr(brand, "M2 Pro") != nullptr)    return HOST_CHIP_M2_PRO;
    if (strstr(brand, "M2") != nullptr)        return HOST_CHIP_M2;
    if (strstr(brand, "M3 Ultra") != nullptr)  return HOST_CHIP_M3_ULTRA;
    if (strstr(brand, "M3 Max") != nullptr)    return HOST_CHIP_M3_MAX;
    if (strstr(brand, "M3 Pro") != nullptr)    return HOST_CHIP_M3_PRO;
    if (strstr(brand, "M3") != nullptr)        return HOST_CHIP_M3;
    if (strstr(brand, "M4 Max") != nullptr)    return HOST_CHIP_M4_MAX;
    if (strstr(brand, "M4 Pro") != nullptr)    return HOST_CHIP_M4_PRO;
    if (strstr(brand, "M4") != nullptr)        return HOST_CHIP_M4;
    if (strstr(brand, "Intel") != nullptr)     return HOST_CHIP_INTEL;
    return HOST_CHIP_UNKNOWN;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)
/** True when a live, usable Vulkan device is attached. */
bool GpuCapability_available(const Device *device) {
    return device != nullptr && Device_isValid(device);
}

/** True when the live device advertises the requested feature. */
bool GpuCapability_has(const Device *device, GpuFeature feature) {
    if (!GpuCapability_available(device))
        return false;
    switch (feature) {
        case GPU_FEATURE_RAY_TRACING:
            return deviceExtension(device, "VK_KHR_ray_tracing_pipeline")
                || deviceExtension(device, "VK_KHR_ray_query");
        case GPU_FEATURE_MESH_SHADERS:
            return deviceExtension(device, "VK_EXT_mesh_shader");
        case GPU_FEATURE_METAL_OBJECTS:
            return Device_hasMetalObjects(device);
        default:
            return false;
    }
}

/** GPU family from the physical-device vendor and the build target. */
GpuFamily GpuCapability_family(const Device *device) {
    if (!GpuCapability_available(device))
        return GPU_FAMILY_UNKNOWN;
    VkPhysicalDevice physical = (VkPhysicalDevice) Device_physical(device);
    if (physical == nullptr)
        return GPU_FAMILY_UNKNOWN;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    if (props.vendorID == 0x106bu) {
        #if defined(VEX_PLATFORM_APPLE_SILICON)
        return GPU_FAMILY_APPLE_SILICON;
        #else
        return GPU_FAMILY_APPLE_INTEL;
        #endif
    }
    return GPU_FAMILY_OTHER;
}

/** The running Apple chip family, or HOST_CHIP_UNKNOWN off Apple. */
HostChip GpuCapability_chip() {
    #if defined(VEX_PLATFORM_APPLE)
    char brand[128];
    bytes size = sizeof brand;
    if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) != 0)
        return HOST_CHIP_UNKNOWN;
    brand[sizeof brand - 1] = '\0';
    return matchChip(brand);
    #else
    return HOST_CHIP_UNKNOWN;
    #endif
}

/** Media-engine presence. No sysctl exposes the block, so this is the stated
 * chip-family inference: every A17 Pro / A18 family and M-series part ships a
 * media engine; Intel Macs do not. An unknown chip answers UNKNOWN, never a
 * guess. A VideoToolbox hardware-decode probe replaces the inference when the
 * host media tier lands (Capability Gating Law, stated fallback). */
HostMediaEngine GpuCapability_mediaEngine() {
    switch (GpuCapability_chip()) {
        case HOST_CHIP_A17_PRO:
        case HOST_CHIP_A18:
        case HOST_CHIP_A18_PRO:
        case HOST_CHIP_M1:
        case HOST_CHIP_M1_PRO:
        case HOST_CHIP_M1_MAX:
        case HOST_CHIP_M1_ULTRA:
        case HOST_CHIP_M2:
        case HOST_CHIP_M2_PRO:
        case HOST_CHIP_M2_MAX:
        case HOST_CHIP_M2_ULTRA:
        case HOST_CHIP_M3:
        case HOST_CHIP_M3_PRO:
        case HOST_CHIP_M3_MAX:
        case HOST_CHIP_M3_ULTRA:
        case HOST_CHIP_M4:
        case HOST_CHIP_M4_PRO:
        case HOST_CHIP_M4_MAX:
            return HOST_MEDIA_ENGINE_PRESENT;
        case HOST_CHIP_INTEL:
            return HOST_MEDIA_ENGINE_ABSENT;
        default:
            return HOST_MEDIA_ENGINE_UNKNOWN;
    }
}

// GETTERS (PUBLIC & PRIVATE)
;;GETTER
/** Stable name for a GPU feature. */
const char *GpuCapability_featureName(GpuFeature feature) {
    switch (feature) {
        case GPU_FEATURE_RAY_TRACING:   return "GPU_FEATURE_RAY_TRACING";
        case GPU_FEATURE_MESH_SHADERS:  return "GPU_FEATURE_MESH_SHADERS";
        case GPU_FEATURE_METAL_OBJECTS: return "GPU_FEATURE_METAL_OBJECTS";
        default:                        return "GPU_FEATURE_UNKNOWN";
    }
}

/** Stable name for a GPU family. */
const char *GpuCapability_familyName(GpuFamily family) {
    switch (family) {
        case GPU_FAMILY_APPLE_SILICON: return "GPU_FAMILY_APPLE_SILICON";
        case GPU_FAMILY_APPLE_INTEL:   return "GPU_FAMILY_APPLE_INTEL";
        case GPU_FAMILY_OTHER:         return "GPU_FAMILY_OTHER";
        default:                       return "GPU_FAMILY_UNKNOWN";
    }
}

/** Stable name for a host chip family. */
const char *GpuCapability_chipName(HostChip chip) {
    switch (chip) {
        case HOST_CHIP_A17_PRO:   return "HOST_CHIP_A17_PRO";
        case HOST_CHIP_A18:       return "HOST_CHIP_A18";
        case HOST_CHIP_A18_PRO:   return "HOST_CHIP_A18_PRO";
        case HOST_CHIP_M1:        return "HOST_CHIP_M1";
        case HOST_CHIP_M1_PRO:    return "HOST_CHIP_M1_PRO";
        case HOST_CHIP_M1_MAX:    return "HOST_CHIP_M1_MAX";
        case HOST_CHIP_M1_ULTRA:  return "HOST_CHIP_M1_ULTRA";
        case HOST_CHIP_M2:        return "HOST_CHIP_M2";
        case HOST_CHIP_M2_PRO:    return "HOST_CHIP_M2_PRO";
        case HOST_CHIP_M2_MAX:    return "HOST_CHIP_M2_MAX";
        case HOST_CHIP_M2_ULTRA:  return "HOST_CHIP_M2_ULTRA";
        case HOST_CHIP_M3:        return "HOST_CHIP_M3";
        case HOST_CHIP_M3_PRO:    return "HOST_CHIP_M3_PRO";
        case HOST_CHIP_M3_MAX:    return "HOST_CHIP_M3_MAX";
        case HOST_CHIP_M3_ULTRA:  return "HOST_CHIP_M3_ULTRA";
        case HOST_CHIP_M4:        return "HOST_CHIP_M4";
        case HOST_CHIP_M4_PRO:    return "HOST_CHIP_M4_PRO";
        case HOST_CHIP_M4_MAX:    return "HOST_CHIP_M4_MAX";
        case HOST_CHIP_INTEL:     return "HOST_CHIP_INTEL";
        default:                  return "HOST_CHIP_UNKNOWN";
    }
}

/** Stable name for a media-engine answer. */
const char *GpuCapability_mediaEngineName(HostMediaEngine engine) {
    switch (engine) {
        case HOST_MEDIA_ENGINE_PRESENT: return "HOST_MEDIA_ENGINE_PRESENT";
        case HOST_MEDIA_ENGINE_ABSENT:  return "HOST_MEDIA_ENGINE_ABSENT";
        default:                        return "HOST_MEDIA_ENGINE_UNKNOWN";
    }
}
