// capability/capability_test.c — the GpuCapability owner contract test.
//
// Pure-query contract: a null Device fails closed for every device question;
// every name is stable and non-empty; host facts answer on Apple Silicon without
// creating a GPU session. A live-device feature scan needs a Vulkan loader and is
// covered by the Device-owned tests, which construct one.

#include "capability/capability.h"
#include "vulkan/device.h"

// following the directory going forward:
// worktree > organization > repository > <directory tree> > file
#include "util/platform.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Test-local Device projections: the capability contract pins the null/invalid
// path, so this binary answers every device question with "no live session".
bool Device_isValid(const Device *device) { (void) device; return false; }
void *Device_physical(const Device *device) { (void) device; return nullptr; }
bool Device_hasMetalObjects(const Device *device) { (void) device; return false; }

int main(void) {
    // A null Device fails closed on every device question.
    CHECK(!GpuCapability_available(nullptr));
    CHECK(!GpuCapability_has(nullptr, GPU_FEATURE_RAY_TRACING));
    CHECK(!GpuCapability_has(nullptr, GPU_FEATURE_MESH_SHADERS));
    CHECK(!GpuCapability_has(nullptr, GPU_FEATURE_METAL_OBJECTS));
    CHECK(GpuCapability_family(nullptr) == GPU_FAMILY_UNKNOWN);
    CHECK(!GpuCapability_has(nullptr, (GpuFeature) 999));
    CHECK(!GpuCapability_has(nullptr, (GpuFeature) -1));

    // Every name is stable, non-empty, and never null.
    for (int i = 0; i < (int) GPU_FEATURE_COUNT; i++)
        CHECK(GpuCapability_featureName((GpuFeature) i)[0] != '\0');
    CHECK(GpuCapability_featureName((GpuFeature) 999)[0] != '\0');
    CHECK(GpuCapability_featureName((GpuFeature) -1)[0] != '\0');
    CHECK(GpuCapability_familyName(GPU_FAMILY_UNKNOWN)[0] != '\0');
    CHECK(GpuCapability_familyName((GpuFamily) 999)[0] != '\0');
    CHECK(GpuCapability_chipName(HOST_CHIP_UNKNOWN)[0] != '\0');
    CHECK(GpuCapability_chipName((HostChip) 999)[0] != '\0');
    CHECK(GpuCapability_mediaEngineName(HOST_MEDIA_ENGINE_UNKNOWN)[0] != '\0');
    CHECK(GpuCapability_mediaEngineName((HostMediaEngine) 999)[0] != '\0');

    // Host facts are pure: repeated reads agree, and they answer without a device.
    HostChip chip = GpuCapability_chip();
    HostMediaEngine media = GpuCapability_mediaEngine();
    CHECK(chip == GpuCapability_chip());
    CHECK(media == GpuCapability_mediaEngine());
    CHECK(GpuCapability_chipName(chip)[0] != '\0');
    CHECK(GpuCapability_mediaEngineName(media)[0] != '\0');

    #if defined(VEX_PLATFORM_APPLE)
    // Apple hosts must identify themselves; an unknown answer is a defect here.
    CHECK(chip != HOST_CHIP_UNKNOWN);
    CHECK(media != HOST_MEDIA_ENGINE_UNKNOWN);
    #endif

    printf("capability_test: chip=%s media=%s\n",
           GpuCapability_chipName(chip), GpuCapability_mediaEngineName(media));

    if (g_failures == 0) {
        printf("capability_test: all assertions held\n");
        return 0;
    }
    printf("capability_test: %d FAILURES\n", g_failures);
    return 1;
}
