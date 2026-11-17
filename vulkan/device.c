#include "vex.h"
#include "vulkan/device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

;;DEFINITION
/** Device owns the Vulkan instance, selected physical and logical device, and
 * graphics queue used by Graphvex's offscreen rendering path. It does not own a
 * swapchain or window presentation surface. Construction probes the host loader,
 * device, graphics queue family and optional platform extensions; partial state
 * is destroyed on rejection. */
;;OVERVIEW
/** ========================================================================
 * CLASS: Device (vulkan/device.c)
 * STRUCT FIELDS (Mirroring vulkan/device.h opaque Device)
 * VkInstance instance           owned Vulkan instance
 * VkPhysicalDevice physical     selected physical device handle
 * VkDevice device               owned logical device
 * VkQueue queue                 borrowed queue retrieved from logical device
 * u32 queueFamily          selected graphics queue-family index
 * bool metalObjects             optional VK_EXT_metal_objects admission result
 * char deviceName[256]          bounded physical-device name projection
 * char error[256]               bounded last construction diagnostic
 * FUNCTION REGISTRY
 * Constructors: Device *Device_create(bool enableValidation)
 * Lifecycle: fn Device_destroy(Device *device)
 * Projections: bool Device_isValid(const Device *device)
 *   const char *Device_lastError(const Device *device)
 *   const char *Device_name(const Device *device)
 *   any Device_native(const Device *device), Device_instance,
 *   Device_physical, Device_queue (opaque Vulkan handles)
 *   u32 Device_queueFamily(const Device *device)
 *   bool Device_hasMetalObjects(const Device *device)
 * Private helper: set_err copies a bounded cold-path failure description.
 * Admission, failure and ownership contract:
 * Device_create returns nullptr after cleaning partial handles on failure;
 * destroy accepts nullptr. The physical handle and queue are owned through the
 * logical device/instance lifetime. No swapchain or presentation resource exists.
 * ======================================================================== */

struct Device {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    u32 queueFamily;
    bool metalObjects;      // VK_EXT_metal_objects enabled (IOSurface/MTL interop)
    char deviceName[256];
    char error[256];
};

;;GETTER
static inline VkInstance *Device_instanceMut(Device *self) { return &(*self).instance; }
;;SETTER
static inline fn Device_setPhysical(Device *self, VkPhysicalDevice v) { (*self).physical = v; }
;;GETTER
static inline VkDevice *Device_deviceMut(Device *self) { return &(*self).device; }
;;GETTER
static inline VkQueue *Device_queueMut(Device *self) { return &(*self).queue; }
;;SETTER
static inline fn Device_setQueueFamily(Device *self, u32 v) { (*self).queueFamily = v; }
;;SETTER
static inline fn Device_setMetalObjects(Device *self, bool v) { (*self).metalObjects = v; }
;;GETTER
static inline char *Device_deviceNameMut(Device *self) { return (*self).deviceName; }
;;GETTER
static inline char *Device_errorMut(Device *self) { return (*self).error; }

// Copies a bounded diagnostic into the device record when one is available.
static fn set_err(Device *c, const char *msg) {
    snprintf(Device_errorMut(c), 256, "%s", msg);
}

Device *Device_create(bool enableValidation) {
    (discard)enableValidation;   // validation layers land in a debug slice
    Device *c = calloc(1, sizeof *c);
    if (!c) return nullptr;
    set_err(c, "ok");

    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "graphvex";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "graphvex";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    // MoltenVK needs the portability enumeration extension + flag.
    const char *exts[1];
    u32 extCount = 0;
    exts[extCount++] = "VK_KHR_portability_enumeration";

    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = extCount;
    ici.ppEnabledExtensionNames = exts;
    ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    if (vkCreateInstance(&ici, nullptr, Device_instanceMut(c)) != VK_SUCCESS) {
        set_err(c, "vkCreateInstance failed (is a Vulkan/MoltenVK driver installed?)");
        Device_destroy(c);
        return nullptr;
    }

    u32 count = 0;
    if (vkEnumeratePhysicalDevices((VkInstance) Device_instance(c), &count, nullptr) != VK_SUCCESS || count == 0) {
        set_err(c, "no Vulkan physical devices");
        Device_destroy(c);
        return nullptr;
    }
    VkPhysicalDevice *devs = calloc(count, sizeof *devs);
    if (!devs) { Device_destroy(c); return nullptr; }
    vkEnumeratePhysicalDevices((VkInstance) Device_instance(c), &count, devs);
    Device_setPhysical(c, devs[0]);
    free(devs);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties((VkPhysicalDevice) Device_physical(c), &props);
    snprintf(Device_deviceNameMut(c), 256, "%s", props.deviceName);

    u32 qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties((VkPhysicalDevice) Device_physical(c), &qn, nullptr);
    VkQueueFamilyProperties *qf = calloc(qn ? qn : 1, sizeof *qf);
    if (!qf) { Device_destroy(c); return nullptr; }
    vkGetPhysicalDeviceQueueFamilyProperties((VkPhysicalDevice) Device_physical(c), &qn, qf);
    i32 chosen = -1;
    for (u32 i = 0; i < qn; i++) {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { chosen = (i32)i; break; }
    }
    free(qf);
    if (chosen < 0) {
        set_err(c, "no graphics queue family");
        Device_destroy(c);
        return nullptr;
    }
    Device_setQueueFamily(c, (u32)chosen);

    f32 prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = Device_queueFamily(c);
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    // MoltenVK advertises VK_KHR_portability_subset; the spec requires enabling
    // it when present. VK_EXT_metal_objects (Apple) lets us import an IOSurface
    // as a render target — the zero-copy seam. Both are enabled only if present.
    u32 extN = 0;
    vkEnumerateDeviceExtensionProperties((VkPhysicalDevice) Device_physical(c), nullptr, &extN, nullptr);
    VkExtensionProperties *devExts = calloc(extN ? extN : 1, sizeof *devExts);
    const char *want[2];
    u32 wantN = 0;
    if (devExts) {
        vkEnumerateDeviceExtensionProperties((VkPhysicalDevice) Device_physical(c), nullptr, &extN, devExts);
        for (u32 i = 0; i < extN; i++) {
            if (!strcmp(devExts[i].extensionName, "VK_KHR_portability_subset"))
                want[wantN++] = "VK_KHR_portability_subset";
            else if (!strcmp(devExts[i].extensionName, "VK_EXT_metal_objects")) {
                want[wantN++] = "VK_EXT_metal_objects";
                Device_setMetalObjects(c, true);
            }
        }
        free(devExts);
    }
    dci.enabledExtensionCount = wantN;
    dci.ppEnabledExtensionNames = wantN ? want : nullptr;

    if (vkCreateDevice((VkPhysicalDevice) Device_physical(c), &dci, nullptr, Device_deviceMut(c)) != VK_SUCCESS) {
        set_err(c, "vkCreateDevice failed");
        Device_destroy(c);
        return nullptr;
    }
    vkGetDeviceQueue((VkDevice) Device_native(c), Device_queueFamily(c), 0, Device_queueMut(c));
    return c;
}

// Destroys owned Vulkan handles and device metadata; null is ignored.
fn Device_destroy(Device *c) {
    if (!c) return;
    if (Device_native(c)) vkDestroyDevice((VkDevice) Device_native(c), nullptr);
    if (Device_instance(c)) vkDestroyInstance((VkInstance) Device_instance(c), nullptr);
    free(c);
}

// Reports whether the wrapper currently owns a non-null logical device.
;;GETTER
bool Device_isValid(const Device *c) { return c && (*c).device != VK_NULL_HANDLE; }
;;GETTER
const char *Device_lastError(const Device *c) { return c ? (*c).error : "null device"; }
;;GETTER
const char *Device_name(const Device *c) { return c ? (*c).deviceName : ""; }
;;GETTER
any Device_native(const Device *c) { return c ? (any) ((*c).device) : nullptr; }
any Device_instance(const Device *c) { return c ? (any) ((*c).instance) : nullptr; }
any Device_physical(const Device *c) { return c ? (any) ((*c).physical) : nullptr; }
any Device_queue(const Device *c) { return c ? (any) ((*c).queue) : nullptr; }
// Returns the selected graphics queue-family index, or zero for null.
u32 Device_queueFamily(const Device *c) { return c ? (*c).queueFamily : 0u; }
// Reports whether the device enabled VK_EXT_metal_objects.
;;GETTER
bool Device_hasMetalObjects(const Device *c) { return c && (*c).metalObjects; }
