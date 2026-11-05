// Owner: SampledImage constructors/chooser/zero, upload/poll/retain/release,
// binding/readiness/device/extent/descriptor and bounded strings. Actual GPU
// sampling proves orientation, alpha, clips, painter order, frame ownership and
// repeated-frame bounded geometry. Adoption is exercised by the gallery fixture.
// Owner-thread only; real device loss/OOM and other platforms remain unproved.
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "vulkan/sampled_image.h"
#include "vulkan/vulkan_backend.h"
#include "graphics/graphics.h"
#include "test_support.h"
#include <vulkan/vulkan.h>

static unsigned forcedTimeouts;
// Injects bounded fence timeouts before delegating to Vulkan's real wait.
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t count,
    const VkFence *fences, VkBool32 all, uint64_t timeout) {
    assert(timeout <= UINT64_C(100000000));
    if (forcedTimeouts) {
        --forcedTimeouts;
        return VK_TIMEOUT;
    }
    PFN_vkWaitForFences real = (PFN_vkWaitForFences) vkGetDeviceProcAddr(device, "vkWaitForFences");
    assert(real && real != vkWaitForFences);
    return real(device, count, fences, all, timeout);
}

// Asserts the RGB value at one pixel in the sampled image.
static void pixel(const Image *image, unsigned x, unsigned y, unsigned r, unsigned g, unsigned b) {
    const uint8_t *p = Image_pixels(image) + y * Image_stride(image) + x * 4u;
    assert(abs((int) p[0] - (int) r) <= 1);
    assert(abs((int) p[1] - (int) g) <= 1);
    assert(abs((int) p[2] - (int) b) <= 1);
    assert(p[3] == 255);
}
// Tests Vulkan image sampling, clipping, and timeout retry behavior.
int main(void) {
    Device *device = VulkanBackend_device();
    if (!Device_isValid(device))
        return B_TEST_SKIP;
    assert(!SampledImage() && !SampledImage_0() && !SampledImage_zero());
    assert(!SampledImage_poll(nullptr) && !SampledImage_retain(nullptr));
    assert(SampledImage_release(nullptr));
    assert(!SampledImage_isReady(nullptr) && !SampledImage_descriptor(nullptr));
    assert(!SampledImage_device(nullptr) && !SampledImage_width(nullptr) && !SampledImage_height(nullptr));
    assert(!SampledImage(nullptr, nullptr));
    assert(!SampledImage(device, nullptr, nullptr, 0, 0));
    Image *image = Image(2, 2);
    const uint8_t rgba[] = {255,0,0,255, 0,255,0,128, 0,0,255,255, 255,255,255,0};
    assert(Image_upload(rgba, 2, 2, image));
    forcedTimeouts = 2;
    SampledImage *pending = SampledImage_2(device, image);
    assert(pending && !SampledImage_isReady(pending));
    assert(!SampledImage_release(pending)); // final release retains ownership
    assert(!Image_gpuResource(image));
    assert(SampledImage_poll(pending) && SampledImage_release(pending));
    SampledImage *sampled = SampledImage(device, image);
    assert(sampled && SampledImage_poll(sampled) && SampledImage_isReady(sampled));
    assert(SampledImage_width(sampled) == 2 && SampledImage_height(sampled) == 2);
    assert(SampledImage_device(sampled) == device && SampledImage_descriptor(sampled));
    Image *wrong = Image(1, 1);
    assert(!SampledImage_bindImage(sampled, wrong));
    Image_destroy(wrong);
    assert(SampledImage_bindImage(sampled, image));
    assert(SampledImage_retain(sampled) && SampledImage_release(sampled));
    char text[2048]; bool cut;
    SampledImage_toString(sampled, text, sizeof text, &cut);
    assert(!cut && strstr(text, "2x2"));
    SampledImage_toStringStruct(sampled, text, sizeof text, &cut);
    assert(!cut && strstr(text, "stagingMemory="));
    SampledImage_toString(sampled, text, 1, &cut);
    assert(cut && !text[0]);
    SampledImage_toString(nullptr, text, sizeof text, &cut);
    assert(!cut && !strcmp(text, "nullptr"));
    SampledImage_toStringStruct(sampled, nullptr, 0, &cut);
    assert(cut);
    assert(SampledImage_release(sampled)); // Image still owns it
    assert(Graphics_register(VulkanBackend_row()) && Graphics_use(BACKEND_VULKAN));
    assert(Graphics_resize(8, 8));
    Image *shot = Image();
    assert(Graphics_begin() && Graphics_clear(COLOR_BLACK));
    for (unsigned i = 0; i < 40; ++i)
        assert(Graphics_drawImage(image, &(Rect){0,0,8,8}));
    assert(Graphics_end() && Graphics_capture(shot));
    pixel(shot, 1,1,255,0,0);
    assert(VulkanBackend_vertexBytes() == 240u * sizeof(VkVertex));
    size_t highWater = VulkanBackend_vertexBytes();
    for (unsigned frame = 0; frame < 32; ++frame) {
        assert(Graphics_begin() && Graphics_clear(COLOR_BLACK));
        assert(Graphics_clip(&(Rect){0,0,8,8}));
        assert(Graphics_drawImage(image, &(Rect){0,0,8,8}));
        assert(Graphics_fillRect(&(Rect){6,0,2,2}, &(Brush){COLOR_WHITE,0,0,0,0}));
        assert(Graphics_end() && Graphics_capture(shot));
        pixel(shot, 1,1,255,0,0);
        pixel(shot, 5,1,0,128,0);
        pixel(shot, 1,5,0,0,255);
        pixel(shot, 5,5,0,0,0);
        pixel(shot, 7,1,255,255,255);
        assert(VulkanBackend_vertexBytes() == highWater);
    }
    Image_fill(image, COLOR_RGBA(0,0,255,255));
    assert(!Image_gpuResource(image));
    assert(VulkanBackend_prepareImage(image));
    assert(Graphics_begin() && Graphics_clear(COLOR_BLACK));
    assert(Graphics_clip(&(Rect){0,0,2,8}));
    assert(Graphics_drawImage(image, &(Rect){0,0,8,8}));
    Image_destroy(image); // recorded frame must pin texture independently
    forcedTimeouts = 1;
    assert(!Graphics_capture(shot));
    assert(Graphics_capture(shot)); // retained command and descriptor retry
    assert(Graphics_end() && Graphics_capture(shot));
    pixel(shot,1,1,0,0,255);
    pixel(shot,3,1,0,0,0);
    Image_destroy(shot);
    VulkanBackend_unbind();
    return 0;
}
