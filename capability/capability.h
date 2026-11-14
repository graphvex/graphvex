#ifndef GRAPHVEX_CAPABILITY_CAPABILITY_H
#define GRAPHVEX_CAPABILITY_CAPABILITY_H

#include "vex.h"

// graphvex R3 — capability/capability.h
//
// Pure capability facts. Every function is a query: no init, no phase, no
// ordering, no cached mutable state. A fact is read; it never transitions.
//
// DEVICE facts are answered from the live Vulkan session (vulkan/device.h).
// HOST facts (chip family, media engine) are borrowed here only until the R0/R1
// host capability class grows its host face; they are pure sysctl reads, need no
// device, and answer UNKNOWN rather than guessing.
//
// RULE: this is not a status object. Nothing here may grow a lifecycle.

typedef struct Device Device;

typedef enum GpuFeature {
    GPU_FEATURE_RAY_TRACING = 0, // VK_KHR_ray_tracing_pipeline or VK_KHR_ray_query
    GPU_FEATURE_MESH_SHADERS,    // VK_EXT_mesh_shader
    GPU_FEATURE_METAL_OBJECTS,   // VK_EXT_metal_objects (Apple zero-copy seam)
    GPU_FEATURE_COUNT
} GpuFeature;

typedef enum GpuFamily {
    GPU_FAMILY_UNKNOWN = 0,
    GPU_FAMILY_APPLE_SILICON,
    GPU_FAMILY_APPLE_INTEL,
    GPU_FAMILY_OTHER
} GpuFamily;

typedef enum HostChip {
    HOST_CHIP_UNKNOWN = 0,
    HOST_CHIP_A17_PRO,
    HOST_CHIP_A18,
    HOST_CHIP_A18_PRO,
    HOST_CHIP_M1,
    HOST_CHIP_M1_PRO,
    HOST_CHIP_M1_MAX,
    HOST_CHIP_M1_ULTRA,
    HOST_CHIP_M2,
    HOST_CHIP_M2_PRO,
    HOST_CHIP_M2_MAX,
    HOST_CHIP_M2_ULTRA,
    HOST_CHIP_M3,
    HOST_CHIP_M3_PRO,
    HOST_CHIP_M3_MAX,
    HOST_CHIP_M3_ULTRA,
    HOST_CHIP_M4,
    HOST_CHIP_M4_PRO,
    HOST_CHIP_M4_MAX,
    HOST_CHIP_INTEL,
    HOST_CHIP_COUNT
} HostChip;

typedef enum HostMediaEngine {
    HOST_MEDIA_ENGINE_UNKNOWN = 0, // this host cannot answer
    HOST_MEDIA_ENGINE_PRESENT,
    HOST_MEDIA_ENGINE_ABSENT
} HostMediaEngine;

// --- Device facts (a null or invalid Device answers false / UNKNOWN) --------
bool      GpuCapability_available(const Device *device);   // a GPU exists and is usable
bool      GpuCapability_has(const Device *device, GpuFeature feature);
GpuFamily GpuCapability_family(const Device *device);

// --- Host facts (no device; pure, side-effect free) -------------------------
HostChip        GpuCapability_chip();
HostMediaEngine GpuCapability_mediaEngine();

// --- Stable symbolic names (cold path; never null) --------------------------
const char *GpuCapability_featureName(GpuFeature feature);
const char *GpuCapability_familyName(GpuFamily family);
const char *GpuCapability_chipName(HostChip chip);
const char *GpuCapability_mediaEngineName(HostMediaEngine engine);

#endif // GRAPHVEX_CAPABILITY_CAPABILITY_H
