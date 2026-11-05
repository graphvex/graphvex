#include "darling/test/compositor/filter_gallery_fixture.h"

#include <assert.h>
#include "test_support.h"
#include "graphics/graphics.h"
#include "vulkan/vulkan_backend.h"

// Numeric fixture/scope proof only: gallery appearance remains user-owned.
static Color pixel(const Image *image, unsigned x, unsigned y) {
    const uint8_t *p = Image_pixels(image) + (size_t) y * Image_stride(image) + (size_t) x * 4;
    return COLOR_RGBA(p[0], p[1], p[2], p[3]);
}

// Checks deterministic gallery fixture creation and rendered sample pixels.
int main(void) {
    assert(!FilterGallery_photoFromPath("/nonexistent-filter-gallery-photo.png", 1, 1));
    assert(!FilterGallery_photoFromPath(nullptr, 0, 1));
    assert(!FilterGallery_photoFromPath(nullptr, UINT32_MAX, UINT32_MAX));
    Device *device=VulkanBackend_device();
    if (!Device_isValid(device)) {
        fprintf(stderr,"filter_gallery_fixture_test: SKIP Vulkan unavailable\n");
        return B_TEST_SKIP;
    }
    char directory[2048];
    assert(FilterGallery_shaderDirectory(directory,sizeof directory));
    GpuScope *gpu=GpuScope(device,directory,FILTER_GALLERY_GPU_PIXEL_BUDGET);
    assert(gpu);
    Image *views[3] = {nullptr};
    Image *textures[3] = {nullptr};
    Image *original = FilterGallery_photo(FILTER_GALLERY_WIDTH, FILTER_GALLERY_HEIGHT);
    assert(original);
    for (unsigned i = 0; i < 3; ++i) {
        assert(FilterGallery_render(gpu,i,&views[i]));
        assert(FilterGallery_renderSampled(gpu,i,&textures[i]));
        assert(Image_isDrawable(textures[i]) && !Image_pixels(textures[i]));
        assert(Image_width(views[i]) == FILTER_GALLERY_WIDTH);
        assert(Image_height(views[i]) == FILTER_GALLERY_HEIGHT);
        Image *caption = FilterGallery_caption(i);
        assert(caption && Image_width(caption) == FILTER_GALLERY_WIDTH);
        assert(pixel(caption, 12, 6) != COLOR_CLEAR);
        Image_destroy(caption);
    }
    // Backdrop blur affects only covered prefix; the picture outside stays sharp.
    assert(pixel(views[0], 12, 120) == pixel(original, 12, 120));
    assert(pixel(views[0], 44, 86) != pixel(original, 44, 86));
    // Foreground blur is clipped by the panel; whole-element blur spills beyond it.
    Color outside = (31 / 12 + 120 / 12) % 2 ? COLOR_RGBA(39, 45, 61, 255) :
                                              COLOR_RGBA(27, 33, 47, 255);
    assert(pixel(views[1], 31, 120) == outside);
    assert(pixel(views[2], 31, 120) != outside);
    assert(pixel(views[1], 32, 130) != pixel(views[2], 32, 130));
    Image *unchanged = original;
    assert(!FilterGallery_render(gpu,3,&unchanged));
    assert(unchanged == original);
    assert(!FilterGallery_render(gpu,0,nullptr));
    assert(!FilterGallery_caption(3));
    assert(GpuScope_destroy(gpu)); // retained outputs outlive all job resources
    assert(Graphics_register(VulkanBackend_row()) && Graphics_use(BACKEND_VULKAN));
    assert(Graphics_resize(FILTER_GALLERY_WIDTH, FILTER_GALLERY_HEIGHT));
    Image *capture = Image();
    for (unsigned i = 0; i < 3; ++i) {
        assert(Graphics_begin() && Graphics_clear(COLOR_CLEAR));
        assert(Graphics_drawImage(textures[i], &(Rect){0,0,FILTER_GALLERY_WIDTH,FILTER_GALLERY_HEIGHT}));
        assert(Graphics_end() && Graphics_capture(capture));
        for (unsigned y = 0; y < FILTER_GALLERY_HEIGHT; ++y)
            for (unsigned x = 0; x < FILTER_GALLERY_WIDTH; ++x)
                assert(pixel(capture,x,y) == pixel(views[i],x,y));
        assert(VulkanBackend_vertexBytes() == 6u * sizeof(VkVertex));
        Image_destroy(textures[i]);
    }
    Image_destroy(capture);
    for (unsigned i = 0; i < 3; ++i)
        Image_destroy(views[i]);
    Image_destroy(original);
    assert(Device_isValid(device));
    VulkanBackend_unbind();
    puts("filter_gallery_fixture_test: PASS actual Vulkan three-scope scatter gallery pixels");
    return 0;
}
