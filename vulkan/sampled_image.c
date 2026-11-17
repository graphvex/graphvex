#include "vex.h"
#include "vulkan/sampled_image.h"
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/* SampledImage owns one immutable completed GPU texture and its compatible
 * single-sampler descriptor. Image and recorded frames hold references; releasing
 * the original Image cannot free a texture still being drawn. CPU input upload
 * is cold and one-shot, never image-content geometry. A timed-out upload retains
 * staging, command pool and fence until polling proves completion. Device is
 * borrowed, and all operations require owner-thread/external synchronization. */
;;OVERVIEW
/* CLASS: SampledImage. Fields, in declaration order: owner (borrowed Device),
 * device (borrowed native device), image/memory/view (owned texture), sampler,
 * layout/pool/set (owned descriptor resources), width/height (native pixels),
 * refs (owner/frame count), commands/fence/staging/stagingMemory (owned upload),
 * pending (upload completion uncertain). No helpers with stored state.
 * Public: _0/_2/_5/chooser/zero; poll, retain, release, isReady, bindImage; width/height,
 * device/descriptor; bounded value/structure strings. Private: memoryType,
 * uploadRelease, dispose, adopt, format. Final release false retains everything
 * on timeout. Cold creation rejects null/invalid/overflow with one THROW;
 * poll/release stay silent and bounded during teardown. */
struct SampledImage {
    Device *owner;
    VkDevice device;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    VkDescriptorSetLayout layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    u32 width, height, refs;
    VkCommandPool commands;
    VkFence fence;
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    bool pending;
};

;;GETTER
static inline Device *SampledImage_owner(const SampledImage *self) { return (*self).owner; }
;;SETTER
static inline fn SampledImage_setOwner(SampledImage *self, Device *v) { (*self).owner = v; }
;;GETTER
static inline VkDevice SampledImage_nativeDevice(const SampledImage *self) { return (*self).device; }
;;SETTER
static inline fn SampledImage_setNativeDevice(SampledImage *self, VkDevice v) { (*self).device = v; }
;;GETTER
static inline VkImage SampledImage_image(const SampledImage *self) { return (*self).image; }
;;SETTER
static inline fn SampledImage_setImage(SampledImage *self, VkImage v) { (*self).image = v; }
;;GETTER
static inline VkDeviceMemory SampledImage_memory(const SampledImage *self) { return (*self).memory; }
;;SETTER
static inline fn SampledImage_setMemory(SampledImage *self, VkDeviceMemory v) { (*self).memory = v; }
;;GETTER
static inline VkImageView SampledImage_view(const SampledImage *self) { return (*self).view; }
;;GETTER
static inline VkImageView *SampledImage_viewMut(SampledImage *self) { return &(*self).view; }
;;GETTER
static inline VkSampler SampledImage_sampler(const SampledImage *self) { return (*self).sampler; }
;;GETTER
static inline VkSampler *SampledImage_samplerMut(SampledImage *self) { return &(*self).sampler; }
;;GETTER
static inline VkDescriptorSetLayout SampledImage_layout(const SampledImage *self) { return (*self).layout; }
;;GETTER
static inline VkDescriptorSetLayout *SampledImage_layoutMut(SampledImage *self) { return &(*self).layout; }
;;GETTER
static inline VkDescriptorPool SampledImage_pool(const SampledImage *self) { return (*self).pool; }
;;GETTER
static inline VkDescriptorPool *SampledImage_poolMut(SampledImage *self) { return &(*self).pool; }
;;GETTER
static inline VkDescriptorSet SampledImage_set(const SampledImage *self) { return (*self).set; }
;;GETTER
static inline VkDescriptorSet *SampledImage_descriptorSetMut(SampledImage *self) { return &(*self).set; }
;;SETTER
static inline fn SampledImage_setWidth(SampledImage *self, u32 v) { (*self).width = v; }
;;SETTER
static inline fn SampledImage_setHeight(SampledImage *self, u32 v) { (*self).height = v; }
;;GETTER
static inline u32 SampledImage_refs(const SampledImage *self) { return (*self).refs; }
;;SETTER
static inline fn SampledImage_setRefs(SampledImage *self, u32 v) { (*self).refs = v; }
;;GETTER
static inline VkCommandPool SampledImage_commands(const SampledImage *self) { return (*self).commands; }
;;GETTER
static inline VkCommandPool *SampledImage_commandsMut(SampledImage *self) { return &(*self).commands; }
;;SETTER
static inline fn SampledImage_setCommands(SampledImage *self, VkCommandPool v) { (*self).commands = v; }
;;GETTER
static inline VkFence SampledImage_fence(const SampledImage *self) { return (*self).fence; }
;;GETTER
static inline VkFence *SampledImage_fenceMut(SampledImage *self) { return &(*self).fence; }
;;SETTER
static inline fn SampledImage_setFence(SampledImage *self, VkFence v) { (*self).fence = v; }
;;GETTER
static inline VkBuffer SampledImage_staging(const SampledImage *self) { return (*self).staging; }
;;GETTER
static inline VkBuffer *SampledImage_stagingMut(SampledImage *self) { return &(*self).staging; }
;;SETTER
static inline fn SampledImage_setStaging(SampledImage *self, VkBuffer v) { (*self).staging = v; }
;;GETTER
static inline VkDeviceMemory SampledImage_stagingMemory(const SampledImage *self) { return (*self).stagingMemory; }
;;GETTER
static inline VkDeviceMemory *SampledImage_stagingMemoryMut(SampledImage *self) { return &(*self).stagingMemory; }
;;SETTER
static inline fn SampledImage_setStagingMemory(SampledImage *self, VkDeviceMemory v) { (*self).stagingMemory = v; }
;;GETTER
static inline bool SampledImage_pending(const SampledImage *self) { return (*self).pending; }
;;SETTER
static inline fn SampledImage_setPending(SampledImage *self, bool v) { (*self).pending = v; }

static const u64 SAMPLE_WAIT_NS = UINT64_C(100000000);
// Selects a compatible physical-device memory type containing every requested property flag.
static u32 memoryType(Device *owner, u32 bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties((VkPhysicalDevice) Device_physical(owner), &properties);
    for (u32 i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & want) == want)
            return i;
    }
    return UINT32_MAX;
}
// Releases staging resources and upload synchronization retained by the pending transfer.
static fn uploadRelease(SampledImage *self) {
    VkDevice d = SampledImage_nativeDevice(self);
    if (SampledImage_commands(self))
        vkDestroyCommandPool(d, SampledImage_commands(self), nullptr);
    if (SampledImage_fence(self))
        vkDestroyFence(d, SampledImage_fence(self), nullptr);
    if (SampledImage_staging(self))
        vkDestroyBuffer(d, SampledImage_staging(self), nullptr);
    if (SampledImage_stagingMemory(self))
        vkFreeMemory(d, SampledImage_stagingMemory(self), nullptr);
    SampledImage_setCommands(self, VK_NULL_HANDLE);
    SampledImage_setFence(self, VK_NULL_HANDLE);
    SampledImage_setStaging(self, VK_NULL_HANDLE);
    SampledImage_setStagingMemory(self, VK_NULL_HANDLE);
}
// Polls the bounded upload fence and retires staging state when the transfer completes.
bool SampledImage_poll(SampledImage *self) {
    if (!self)
        return false;
    if (SampledImage_pending(self)) {
        if (vkWaitForFences(SampledImage_nativeDevice(self), 1, SampledImage_fenceMut(self), VK_TRUE, SAMPLE_WAIT_NS) != VK_SUCCESS)
            return false;
        SampledImage_setPending(self, false);
        uploadRelease(self);
    }
    return true;
}
// Releases sampled-image resources, optionally destroying an internally owned VkImage.
static fn dispose(SampledImage *self, bool ownedImage) {
    VkDevice d = SampledImage_nativeDevice(self);
    uploadRelease(self);
    if (SampledImage_pool(self))
        vkDestroyDescriptorPool(d, SampledImage_pool(self), nullptr);
    if (SampledImage_layout(self))
        vkDestroyDescriptorSetLayout(d, SampledImage_layout(self), nullptr);
    if (SampledImage_sampler(self))
        vkDestroySampler(d, SampledImage_sampler(self), nullptr);
    if (SampledImage_view(self))
        vkDestroyImageView(d, SampledImage_view(self), nullptr);
    if (ownedImage && SampledImage_image(self))
        vkDestroyImage(d, SampledImage_image(self), nullptr);
    if (ownedImage && SampledImage_memory(self))
        vkFreeMemory(d, SampledImage_memory(self), nullptr);
    free(self);
}
// Adds a reference to a valid sampled image unless its reference count is saturated.
bool SampledImage_retain(SampledImage *self) {
    if (!self || SampledImage_refs(self) == UINT32_MAX)
        return false;
    SampledImage_setRefs(self, SampledImage_refs(self) + 1);
    return true;
}
// Drops a reference and disposes the sampled image when the count reaches zero.
bool SampledImage_release(SampledImage *self) {
    if (!self)
        return true;
    if (SampledImage_refs(self) == 1) {
        if (!SampledImage_poll(self))
            return false;
        dispose(self, true);
    } else
        SampledImage_setRefs(self, SampledImage_refs(self) - 1);
    return true;
}
// Reports whether the asynchronous upload is no longer pending.
bool SampledImage_isReady(const SampledImage *self) { return self && !SampledImage_pending(self); }
// Adapts Image's opaque retain callback to a SampledImage reference increment.
static bool retainOpaque(any resource) { return SampledImage_retain(resource); }
// Adapts Image's opaque release callback to a SampledImage reference decrement.
static bool releaseOpaque(any resource) { return SampledImage_release(resource); }
// Binds this resource into an Image, transferring one retained reference through its callback pair.
bool SampledImage_bindImage(SampledImage *self, Image *image) {
    if (!SampledImage_isReady(self) || !image || Image_width(image) != SampledImage_width(self) ||
        Image_height(image) != SampledImage_height(self)) {
        THROW("SampledImage binding requires matching completed image");
        return false;
    }
    return Image_bindGpu(image, self, SampledImage_owner(self), (any) SampledImage_set(self), retainOpaque, releaseOpaque);
}
// Returns the sampled image width, or zero for null.
u32 SampledImage_width(const SampledImage *self) { return self ? (*self).width : 0; }
// Returns the sampled image height, or zero for null.
u32 SampledImage_height(const SampledImage *self) { return self ? (*self).height : 0; }
Device *SampledImage_device(const SampledImage *self) { return self ? SampledImage_owner(self) : nullptr; }
any SampledImage_descriptor(const SampledImage *self) { return self ? (any) SampledImage_set(self) : nullptr; }
SampledImage *SampledImage_0() { return nullptr; }
SampledImage *SampledImage_zero() { return SampledImage_0(); }

static SampledImage *adopt(Device *owner, VkImage image, VkDeviceMemory memory,
                           u32 width, u32 height) {
    SampledImage *self = calloc(1, sizeof *self);
    if (!self)
        return nullptr;
    SampledImage_setOwner(self, owner);
    SampledImage_setNativeDevice(self, (VkDevice) Device_native(owner));
    SampledImage_setImage(self, image);
    SampledImage_setMemory(self, memory);
    SampledImage_setWidth(self, width);
    SampledImage_setHeight(self, height);
    SampledImage_setRefs(self, 1);
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size};
    VkDevice d = SampledImage_nativeDevice(self);
    if (vkCreateImageView(d, &view, nullptr, SampledImage_viewMut(self)) != VK_SUCCESS ||
        vkCreateSampler(d, &sampler, nullptr, SampledImage_samplerMut(self)) != VK_SUCCESS ||
        vkCreateDescriptorSetLayout(d, &layout, nullptr, SampledImage_layoutMut(self)) != VK_SUCCESS ||
        vkCreateDescriptorPool(d, &pool, nullptr, SampledImage_poolMut(self)) != VK_SUCCESS)
        goto failed;
    VkDescriptorSetAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = SampledImage_pool(self), .descriptorSetCount = 1, .pSetLayouts = SampledImage_layoutMut(self)};
    if (vkAllocateDescriptorSets(d, &allocate, SampledImage_descriptorSetMut(self)) != VK_SUCCESS)
        goto failed;
    VkDescriptorImageInfo info = {SampledImage_sampler(self), SampledImage_view(self), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = SampledImage_set(self), .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &info};
    vkUpdateDescriptorSets(d, 1, &write, 0, nullptr);
    return self;
failed:
    dispose(self, false);
    return nullptr;
}
SampledImage *SampledImage_5(Device *owner, any image, any memory, u32 width, u32 height) {
    if (!Device_isValid(owner) || !image || !memory || !width || !height) {
        THROW("SampledImage adoption requires a completed device-owned texture");
        return nullptr;
    }
    SampledImage *self = adopt(owner, (VkImage) image, (VkDeviceMemory) memory, width, height);
    if (!self)
        THROW("SampledImage descriptor allocation failed");
    return self;
}
SampledImage *SampledImage_2(Device *owner, const Image *source) {
    u32 w = Image_width(source), h = Image_height(source);
    bytes stride = Image_stride(source);
    if (!Device_isValid(owner) || !Image_isValid(source) || !Image_pixels(source) ||
        Image_format(source) != IMAGE_FORMAT_RGBA8 || !w || !h ||
        (u64) w * h > SIZE_MAX / 4u || stride < (bytes) w * 4u || stride > SIZE_MAX / h) {
        THROW("SampledImage upload rejected invalid RGBA shadow");
        return nullptr;
    }
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties((VkPhysicalDevice) Device_physical(owner), &properties);
    if (w > properties.limits.maxImageDimension2D || h > properties.limits.maxImageDimension2D) {
        THROW("SampledImage upload exceeds device extent");
        return nullptr;
    }
    VkDevice d = (VkDevice) Device_native(owner);
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageCreateInfo texture = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {w,h,1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    SampledImage *self = nullptr;
    if (vkCreateImage(d, &texture, nullptr, &image) != VK_SUCCESS)
        goto failed;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(d, image, &requirements);
    u32 type = memoryType(owner, requirements.memoryTypeBits, 0);
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type};
    if (type == UINT32_MAX || vkAllocateMemory(d, &allocation, nullptr, &memory) != VK_SUCCESS ||
        vkBindImageMemory(d, image, memory, 0) != VK_SUCCESS)
        goto failed;
    self = adopt(owner, image, memory, w, h);
    if (!self)
        goto failed;
    VkDeviceSize Bytes = (VkDeviceSize) w * h * 4u;
    VkBufferCreateInfo staging = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = Bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    if (vkCreateBuffer(d, &staging, nullptr, SampledImage_stagingMut(self)) != VK_SUCCESS)
        goto failed;
    vkGetBufferMemoryRequirements(d, SampledImage_staging(self), &requirements);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(owner, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocation.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(d, &allocation, nullptr, SampledImage_stagingMemoryMut(self)) != VK_SUCCESS ||
        vkBindBufferMemory(d, SampledImage_staging(self), SampledImage_stagingMemory(self), 0) != VK_SUCCESS)
        goto failed;
    any mappedPixelBytes = nullptr;
    if (vkMapMemory(d, SampledImage_stagingMemory(self), 0, Bytes, 0, &mappedPixelBytes) != VK_SUCCESS)
        goto failed;
    for (u32 y = 0; y < h; ++y)
        memcpy((u8*) mappedPixelBytes + (bytes) y * w * 4u,
            Image_pixels(source) + (bytes) y * stride, (bytes) w * 4u);
    vkUnmapMemory(d, SampledImage_stagingMemory(self));
    VkCommandPoolCreateInfo commands = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = Device_queueFamily(owner)};
    VkCommandBufferAllocateInfo commandInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateCommandPool(d, &commands, nullptr, SampledImage_commandsMut(self)) != VK_SUCCESS ||
        vkCreateFence(d, &fence, nullptr, SampledImage_fenceMut(self)) != VK_SUCCESS)
        goto failed;
    commandInfo.commandPool = SampledImage_commands(self);
    if (vkAllocateCommandBuffers(d, &commandInfo, &command) != VK_SUCCESS)
        goto failed;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS)
        goto failed;
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}, .imageExtent = {w,h,1}};
    vkCmdCopyBufferToImage(command, SampledImage_staging(self), image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    if (vkEndCommandBuffer(command) != VK_SUCCESS)
        goto failed;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    VkResult result = vkQueueSubmit((VkQueue) Device_queue(owner), 1, &submit, SampledImage_fence(self));
    if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
        goto failed;
    SampledImage_setPending(self, true);
    SampledImage_poll(self);
    return self;
failed:
    if (self)
        dispose(self, true);
    else {
        if (image)
            vkDestroyImage(d, image, nullptr);
        if (memory)
            vkFreeMemory(d, memory, nullptr);
    }
    THROW("SampledImage GPU upload allocation or recording failed");
    return nullptr;
}
// Writes the bounded value or one-level field projection of a sampled image.
static fn format(const SampledImage *self, bool structure, char *dest, bytes cap, bool *truncated) {
    i32 n = 0;
    if (!dest || !cap) {
        if (truncated)
            *truncated = true;
        return;
    }
    if (!self)
        n = snprintf(dest, cap, "nullptr");
    else if (!structure)
        n = snprintf(dest, cap, "SampledImage(%ux%u,refs=%u,pending=%d)",
            SampledImage_width(self), SampledImage_height(self), SampledImage_refs(self), SampledImage_pending(self));
    else
        n = snprintf(dest, cap, "SampledImage{owner=%p,device=%p,image=%p,memory=%p,view=%p,sampler=%p,layout=%p,pool=%p,set=%p,width=%u,height=%u,refs=%u,commands=%p,fence=%p,staging=%p,stagingMemory=%p,pending=%d}",
            (any) SampledImage_owner(self), (any) SampledImage_nativeDevice(self), (any) SampledImage_image(self), (any) SampledImage_memory(self),
            (any) SampledImage_view(self), (any) SampledImage_sampler(self), (any) SampledImage_layout(self), (any) SampledImage_pool(self),
            (any) SampledImage_set(self), SampledImage_width(self), SampledImage_height(self), SampledImage_refs(self), (any) SampledImage_commands(self),
            (any) SampledImage_fence(self), (any) SampledImage_staging(self), (any) SampledImage_stagingMemory(self), SampledImage_pending(self));
    if (truncated)
        *truncated = n < 0 || (bytes) n >= cap;
}
// Formats a concise bounded summary of the sampled image.
fn SampledImage_toString(const SampledImage *self, char *dest, bytes cap, bool *truncated) { format(self,false,dest,cap,truncated); }
// Formats the sampled-image fields into caller-provided bounded storage.
fn SampledImage_toStringStruct(const SampledImage *self, char *dest, bytes cap, bool *truncated) { format(self,true,dest,cap,truncated); }
