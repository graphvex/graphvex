/* Owner: compositor/color_pass.{c,h}, shaders/compositor/color.frag.
 * Actual Vulkan sampled-texture -> f32 attachment -> readback numeric proof.
 * No CPU filter execution. CPU arithmetic below is a test-only independent
 * oracle. Shader/descriptor/barrier/record paths execute on the real device.
 * Legal use is externally synchronized; 100ms fence bound + runner watchdog.
 * Covers supported forms/alpha/HDR/order/rejection/retry/borrowed lifetime.
 * Gaps: OOM injection, automatic tree/scope planner and other platforms. */
#include "../../compositor/color_pass.h"
#include "test_support.h"
#include <vulkan/vulkan.h>
#include <assert.h>
#include <f32.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VK(call) assert((call) == VK_SUCCESS)
enum { WIDTH = 8, HEIGHT = 2, COMPONENTS = WIDTH * HEIGHT * 4 };
static const VkDeviceSize BYTES = COMPONENTS * sizeof(f32);
static VkDevice device;
static VkPhysicalDevice physical;

typedef struct TestImage {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFramebuffer framebuffer;
} TestImage;

// Finds a compatible Vulkan memory type for the requested properties.
static u32 memoryType(u32 bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (u32 i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryType *type = &properties.memoryTypes[i];
        if ((bits & (1u << i)) && ((*type).propertyFlags & flags) == flags)
            return i;
    }
    assert(false && "required test memory type unavailable");
    return 0;
}
// Creates and binds a test buffer with host-visible memory.
static void buffer(VkBufferUsageFlags usage, VkBuffer *out, VkDeviceMemory *memory) {
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = BYTES, .usage = usage};
    VK(vkCreateBuffer(device, &info, nullptr, out));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *out, &requirements);
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryType(requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VK(vkAllocateMemory(device, &allocation, nullptr, memory));
    VK(vkBindBufferMemory(device, *out, *memory, 0));
}
// Creates a color image and framebuffer for one filter-pass fixture.
static TestImage image(VkRenderPass pass) {
    TestImage result = {0};
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .extent = {WIDTH, HEIGHT, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VK(vkCreateImage(device, &info, nullptr, &result.image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, result.image, &requirements);
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryType(requirements.memoryTypeBits, 0)};
    VK(vkAllocateMemory(device, &allocation, nullptr, &result.memory));
    VK(vkBindImageMemory(device, result.image, result.memory, 0));
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = result.image, .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VK(vkCreateImageView(device, &view, nullptr, &result.view));
    VkFramebufferCreateInfo fb = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = pass, .attachmentCount = 1, .pAttachments = &result.view,
        .width = WIDTH, .height = HEIGHT, .layers = 1};
    VK(vkCreateFramebuffer(device, &fb, nullptr, &result.framebuffer));
    return result;
}
// Releases the image fixture's framebuffer, view, image, and memory.
static void destroyImage(TestImage *self) {
    vkDestroyFramebuffer(device, (*self).framebuffer, nullptr);
    vkDestroyImageView(device, (*self).view, nullptr);
    vkDestroyImage(device, (*self).image, nullptr);
    vkFreeMemory(device, (*self).memory, nullptr);
}
// Records an image layout transition and its access dependency.
static void transition(VkCommandBuffer cmd, VkImage target, VkImageLayout old,
                       VkImageLayout next, VkAccessFlags fromAccess, VkAccessFlags toAccess,
                       VkPipelineStageFlags fromStage, VkPipelineStageFlags toStage) {
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = fromAccess, .dstAccessMask = toAccess, .oldLayout = old,
        .newLayout = next, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = target,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cmd, fromStage, toStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
// Loads a staged SPIR-V module into an owned word buffer.
static u32 *words(const char *name, bytes *size) {
    const char *home = getenv("B_HOME");
    char path[2048];
    if (home)
        snprintf(path, sizeof path, "%s/out/debug/shader/compositor/%s.spv", home, name);
    else
        snprintf(path, sizeof path, "%s/Library/Application Support/vexgraph/b.sh/out/debug/shader/compositor/%s.spv",
                 getenv("HOME"), name);
    FILE *file = fopen(path, "rb");
    assert(file && "b.sh must generate compositor shaders before test");
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0 && length % 4 == 0);
    rewind(file);
    u32 *data = malloc((bytes) length);
    assert(data && fread(data, 1, (bytes) length, file) == (bytes) length);
    fclose(file); *size = (bytes) length;
    return data;
}
// Computes the independent CPU reference for one color-filter token.
static void oracle(const f32 *p, FilterToken token, f32 *out) {
    d64 alpha = p[3];
    d64 rgb[3] = {0};
    for (unsigned c = 0; c < 3; ++c)
        rgb[c] = alpha > 0 ? p[c] / alpha : 0;
    u32 bits = (u32) Filter_payload(token);
    f32 amount; memcpy(&amount, &bits, sizeof amount);
    u16 id = Filter_id(token);
    d64 gray = (2126 * rgb[0] + 7152 * rgb[1] + 722 * rgb[2]) / 10000;
    if (id == GRAYSCALE_RED_ID) gray = rgb[0];
    if (id == GRAYSCALE_GREEN_ID) gray = rgb[1];
    if (id == GRAYSCALE_BLUE_ID) gray = rgb[2];
    if (id == BLACK_AND_WHITE_ID) gray = gray >= amount ? 1 : 0;
    for (unsigned c = 0; c < 3; ++c) {
        d64 value = gray;
        if (id == BRIGHTNESS_ID) value = rgb[c] + amount;
        if (id == CONTRAST_ID) value = (rgb[c] - .5) * amount + .5;
        if (id == INVERT_ID) value = 1 - rgb[c];
        if (id == BRIGHTNESS_ID || id == CONTRAST_ID || id == INVERT_ID)
            value = fmin(1, fmax(0, value));
        out[c] = (f32) (alpha > 0 ? fmin(FLT_MAX, value * alpha) : 0);
    }
    out[3] = p[3];
}
// Records one ColorPass operation into the supplied Vulkan command buffer.
static void draw(ColorPass *pass, VkCommandBuffer cmd, VkRenderPass render,
                 TestImage *target, VkDescriptorSet descriptor, FilterToken token) {
    VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render, .framebuffer = (*target).framebuffer,
        .renderArea = {{0, 0}, {WIDTH, HEIGHT}}};
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    assert(ColorPass_record(pass, (void*) cmd, (void*) descriptor, WIDTH, HEIGHT, token));
    vkCmdEndRenderPass(cmd);
}

// Compares Vulkan color-pass pixels with CPU references and rejection cases.
int main(void) {
    assert(ColorPass_zero() == nullptr);
    assert(ColorPass_0() == nullptr && ColorPass() == nullptr);
    assert(ColorPass_getDescriptorLayout(nullptr) == nullptr);
    ColorPass_destroy(nullptr);
    Device *session = Device_create(false);
    if (!Device_isValid(session)) {
        fprintf(stderr, "color_pass_test: SKIP Vulkan unavailable: %s\n", Device_lastError(session));
        Device_destroy(session); return B_TEST_SKIP;
    }
    device = (VkDevice) Device_native(session);
    physical = (VkPhysicalDevice) Device_physical(session);
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R32G32B32A32_SFLOAT, &properties);
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if ((properties.optimalTilingFeatures & required) != required) {
        fprintf(stderr, "color_pass_test: SKIP sampled f32 target unsupported\n");
        Device_destroy(session); return B_TEST_SKIP;
    }
    VkAttachmentDescription attachment = {.format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &reference};
    VkSubpassDependency dependencies[] = {
        {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
         .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
        {.srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT}};
    VkRenderPassCreateInfo rp = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1,
        .pSubpasses = &subpass, .dependencyCount = 2, .pDependencies = dependencies};
    VkRenderPass render; VK(vkCreateRenderPass(device, &rp, nullptr, &render));
    bytes vertexSize, fragmentSize;
    u32 *vertex = words("resolve.vert", &vertexSize), *fragment = words("color.frag", &fragmentSize);
    ColorPass *pass = ColorPass(session, (void*) render, vertex, vertexSize, fragment, fragmentSize);
    assert(pass);
    ColorPass *independent = ColorPass_6(session, (void*) render, vertex, vertexSize, fragment, fragmentSize);
    assert(independent && independent != pass);
    ColorPass_destroy(independent);
    char text[256]; bool truncated;
    ColorPass_toString(pass, text, sizeof text, &truncated);
    assert(!truncated && strstr(text, "Vulkan"));
    ColorPass_toStringStruct(pass, text, sizeof text, &truncated);
    assert(!truncated && strstr(text, "device=") && strstr(text, "descriptorLayout=") && strstr(text, "layout=") && strstr(text, "pipeline="));
    ColorPass_toString(pass, text, 1, &truncated); assert(truncated && text[0] == 0);
    ColorPass_toStringStruct(nullptr, text, sizeof text, &truncated); assert(!strcmp(text, "nullptr"));

    TestImage input = image(render), output = image(render), second = image(render);
    VkBuffer upload, readback; VkDeviceMemory uploadMemory, readbackMemory;
    buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &upload, &uploadMemory);
    buffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback, &readbackMemory);
    const f32 pixels[COMPONENTS] = {
        0,0,0,0, .125f,.375f,.875f,1, .125f,.25f,.375f,.5f, 1,0,0,1,
        0,1,0,1, 0,0,1,1, 2,1,.5f,.5f, .5f,.5f,.5f,1,
        .25f,0,0,.25f, 0,.25f,0,.25f, 0,0,.25f,.25f, 1,1,1,1,
        FLT_MAX,FLT_MAX,FLT_MAX,1, .75f,.125f,.5f,1, 0,0,0,1,
        1.25e-9f,3.75e-9f,8.75e-9f,1e-8f};
    void *mapped;
    VK(vkMapMemory(device, uploadMemory, 0, BYTES, 0, &mapped));
    memcpy(mapped, pixels, sizeof pixels); vkUnmapMemory(device, uploadMemory);
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VkSampler sampler; VK(vkCreateSampler(device, &si, nullptr, &sampler));
    VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo dp = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &poolSize};
    VkDescriptorPool descriptorPool; VK(vkCreateDescriptorPool(device, &dp, nullptr, &descriptorPool));
    VkDescriptorSetLayout layouts[] = {(VkDescriptorSetLayout) ColorPass_getDescriptorLayout(pass),
        (VkDescriptorSetLayout) ColorPass_getDescriptorLayout(pass)};
    VkDescriptorSetAllocateInfo da = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool, .descriptorSetCount = 2, .pSetLayouts = layouts};
    VkDescriptorSet descriptors[2]; VK(vkAllocateDescriptorSets(device, &da, descriptors));
    VkDescriptorImageInfo images[] = {{sampler, input.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                    {sampler, output.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    for (unsigned i = 0; i < 2; ++i) {
        VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptors[i], .dstBinding = 0, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &images[i]};
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    VkCommandPoolCreateInfo cp = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = Device_queueFamily(session)};
    VkCommandPool commandPool; VK(vkCreateCommandPool(device, &cp, nullptr, &commandPool));
    VkCommandBufferAllocateInfo ca = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cmd; VK(vkAllocateCommandBuffers(device, &ca, &cmd));
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence; VK(vkCreateFence(device, &fi, nullptr, &fence));
    FilterToken tokens[] = {Filter_brightness(-1), Filter_brightness(-0.0f), Filter_brightness(.25f),
        Filter_brightness(1), Filter_contrast(0), Filter_contrast(1), Filter_contrast(2),
        Filter_contrast(FLT_MAX), Filter_grayscale(), Filter_grayscaleRed(),
        Filter_grayscaleGreen(), Filter_grayscaleBlue(), Filter_invert(),
        Filter_blackAndWhite(0), Filter_blackAndWhite(.5f), Filter_blackAndWhite(1)};
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {WIDTH, HEIGHT, 1}};
    for (bytes iteration = 0; iteration < sizeof tokens / sizeof tokens[0] + 2; ++iteration) {
        bool ordered = iteration >= sizeof tokens / sizeof tokens[0];
        bool reverse = iteration == sizeof tokens / sizeof tokens[0] + 1;
        FilterToken first = ordered ? (reverse ? Filter_contrast(2) : Filter_brightness(.25f)) : tokens[iteration];
        FilterToken last = reverse ? Filter_brightness(.25f) : Filter_contrast(2);
        assert(ColorPass_validateToken(first));
        VK(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK(vkBeginCommandBuffer(cmd, &begin));
        if (iteration == 0) {
            transition(cmd, input.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            vkCmdCopyBufferToImage(cmd, upload, input.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            transition(cmd, input.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
        draw(pass, cmd, render, &output, descriptors[0], first);
        TestImage *final = &output;
        if (ordered) {
            transition(cmd, output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            draw(pass, cmd, render, &second, descriptors[1], last);
            final = &second;
        }
        vkCmdCopyImageToBuffer(cmd, (*final).image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = readback, .size = BYTES};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &host, 0, nullptr);
        VK(vkEndCommandBuffer(cmd));
        VK(vkResetFences(device, 1, &fence));
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        VK(vkQueueSubmit((VkQueue) Device_queue(session), 1, &submit, fence));
        VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(100000000)));
        VK(vkMapMemory(device, readbackMemory, 0, BYTES, 0, &mapped));
        const f32 *actual = mapped;
        for (unsigned p = 0; p < WIDTH * HEIGHT; ++p) {
            f32 expected[4], temporary[4];
            oracle(pixels + 4 * p, first, expected);
            if (ordered) {
                memcpy(temporary, expected, sizeof temporary);
                oracle(temporary, last, expected);
            }
            for (unsigned c = 0; c < 4; ++c) {
                d64 tolerance = fmax(1e-14, fabs(expected[c]) * 3e-6);
                assert(isfinite(actual[p * 4 + c]) && fabs(actual[p * 4 + c] - expected[c]) <= tolerance);
            }
            assert(actual[p * 4 + 3] == pixels[p * 4 + 3]);
        }
        vkUnmapMemory(device, readbackMemory);
    }
    FILE *diagnostics = tmpfile(); assert(diagnostics);
    int saved = dup(STDERR_FILENO); assert(saved >= 0);
    assert(dup2(fileno(diagnostics), STDERR_FILENO) >= 0);
    const FilterToken invalid[] = {Filter_brightness(NAN), Filter_brightness(2),
        Filter_contrast(-1), Filter_contrast(INFINITY), Filter_blackAndWhite(-1),
        Filter_blackAndWhite(2), Filter_grayscale() | 1,
        Filter_brightness(0) | (UINT64_C(1) << 32), Filter_hsl(1,1)};
    for (bytes i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        assert(!ColorPass_validateToken(invalid[i]));
    assert(!ColorPass_record(nullptr, (void*) cmd, (void*) descriptors[0], WIDTH, HEIGHT, tokens[0]));
    assert(!ColorPass_record(pass, nullptr, (void*) descriptors[0], WIDTH, HEIGHT, tokens[0]));
    assert(!ColorPass_record(pass, (void*) cmd, nullptr, WIDTH, HEIGHT, tokens[0]));
    assert(!ColorPass_record(pass, (void*) cmd, (void*) descriptors[0], 0, HEIGHT, tokens[0]));
    assert(!ColorPass_record(pass, (void*) cmd, (void*) descriptors[0], WIDTH, HEIGHT, invalid[0]));
    assert(!ColorPass(nullptr, (void*) render, vertex, vertexSize, fragment, fragmentSize));
    assert(!ColorPass(session, nullptr, vertex, vertexSize, fragment, fragmentSize));
    assert(!ColorPass(session, (void*) render, nullptr, vertexSize, fragment, fragmentSize));
    assert(!ColorPass(session, (void*) render, vertex, vertexSize, nullptr, fragmentSize));
    assert(!ColorPass(session, (void*) render, vertex, 0, fragment, fragmentSize));
    assert(!ColorPass(session, (void*) render, vertex, vertexSize, fragment, fragmentSize - 1));
    u32 badWords[5] = {0};
    assert(!ColorPass(session, (void*) render, badWords, sizeof badWords, fragment, fragmentSize));
    fflush(stderr); assert(dup2(saved, STDERR_FILENO) >= 0); close(saved);
    rewind(diagnostics); unsigned lines = 0;
    while (fgets(text, sizeof text, diagnostics)) { assert(strstr(text, "[vex]") && strstr(text, "ColorPass")); ++lines; }
    assert(lines == sizeof invalid / sizeof invalid[0] + 12); fclose(diagnostics);
    assert(ColorPass_validateToken(Filter_contrast(1)));
    /* Recover with actual GPU work, not merely a subsequent validator call. */
    VK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo retry = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK(vkBeginCommandBuffer(cmd, &retry));
    draw(pass, cmd, render, &second, descriptors[0], Filter_grayscale());
    vkCmdCopyImageToBuffer(cmd, second.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
    VkBufferMemoryBarrier retryHost = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = readback, .size = BYTES};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &retryHost, 0, nullptr);
    VK(vkEndCommandBuffer(cmd));
    VK(vkResetFences(device, 1, &fence));
    VkSubmitInfo retrySubmit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cmd};
    VK(vkQueueSubmit((VkQueue) Device_queue(session), 1, &retrySubmit, fence));
    VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(100000000)));

    VK(vkMapMemory(device, readbackMemory, 0, BYTES, 0, &mapped));
    const f32 *recovered = mapped;
    for (unsigned p = 0; p < WIDTH * HEIGHT; ++p) {
        f32 expected[4];
        oracle(pixels + 4 * p, Filter_grayscale(), expected);
        for (unsigned c = 0; c < 4; ++c)
            assert(fabs(recovered[p * 4 + c] - expected[c]) <= fmax(1e-14, fabs(expected[c]) * 3e-6));
    }
    vkUnmapMemory(device, readbackMemory);

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroySampler(device, sampler, nullptr);
    ColorPass_destroy(pass);
    assert(Device_isValid(session)); /* borrowed device survives */
    destroyImage(&second); destroyImage(&output); destroyImage(&input);
    vkDestroyBuffer(device, readback, nullptr); vkFreeMemory(device, readbackMemory, nullptr);
    vkDestroyBuffer(device, upload, nullptr); vkFreeMemory(device, uploadMemory, nullptr);
    vkDestroyRenderPass(device, render, nullptr);
    free(vertex); free(fragment);
    printf("color_pass_test: PASS on %s (Vulkan texture shader pixels, order, rejection)\n", Device_name(session));
    Device_destroy(session);
    return 0;
}
