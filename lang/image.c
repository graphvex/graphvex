#include "vex.h"
#include "lang/image.h"

#include <stdlib.h>
#include <string.h>

// graphvex R3 — image.c
;;DEFINITION
/* Image is the backend-neutral pixel identity: optional CPU RGBA shadow plus
 * an owned reference to an immutable SampledImage. GPU-only filter outputs need
 * no readback to become drawable. CPU edits invalidate the sampled reference;
 * a recorded frame retains its own reference until its fence completes. */
;;OVERVIEW
/* CLASS: Image. Fields: width,height (logical native extent), capW,capH (CPU
 * allocation/stride), format,usage (pixel contract), pixels (owned CPU shadow),
 * native (borrowed dialect handle), ioSurface (borrowed native surface), layer
 * (legacy atlas index), gpuResource (owned opaque texture), gpuDevice (borrowed
 * owner identity), gpuDescriptor (borrowed from texture), gpuRetain/gpuRelease
 * (reference callbacks). Public: existing
 * constructors/new/destroy; resize/ensureShadow/upload/fill; dimensions/format/
 * usage/stride/pixels/validity/native/IOSurface/layer; bindGpu/clearGpu and
 * gpuResource/gpuDevice/gpuDescriptor/isDrawable. Admission validates resource
 * dimensions/readiness in its owning driver; bind rejects incomplete fn-tables.
 * Owner-thread or external synchronization. Raw pixel edits clear the sampled
 * binding explicitly; borrowed native fields carry no lifetime ownership. */

struct Image {
    u32 width;
    u32 height;
    u32 capW;         // allocation width (grow-only); row stride is capW*4
    u32 capH;         // allocation height
    u32 format;
    u32 usage;
    u8 *pixels;       // CPU shadow (RGBA8)
    any native;          // opaque dialect handle
    any ioSurface;       // opaque IOSurfaceRef
    u32 layer;        // atlas layer / sampler index
    any gpuResource; // owned opaque texture; renderer retains independently
    any gpuDevice; // borrowed driver/device identity
    any gpuDescriptor; // borrowed descriptor from owned resource
    ImageGpuRefFn gpuRetain;
    ImageGpuRefFn gpuRelease;
};

;;SETTER
static inline fn Image_setWidth(Image *self, u32 v) { (*self).width = v; }
;;SETTER
static inline fn Image_setHeight(Image *self, u32 v) { (*self).height = v; }
;;GETTER
static inline u32 Image_capW(const Image *self) { return (*self).capW; }
;;SETTER
static inline fn Image_setCapW(Image *self, u32 v) { (*self).capW = v; }
;;GETTER
static inline u32 Image_capH(const Image *self) { return (*self).capH; }
;;SETTER
static inline fn Image_setCapH(Image *self, u32 v) { (*self).capH = v; }
;;SETTER
static inline fn Image_setFormat(Image *self, u32 v) { (*self).format = v; }
;;SETTER
static inline fn Image_setUsage(Image *self, u32 v) { (*self).usage = v; }
;;SETTER
static inline fn Image_setPixels(Image *self, u8 *v) { (*self).pixels = v; }
;;SETTER
static inline fn Image_setGpuResource(Image *self, any v) { (*self).gpuResource = v; }
;;SETTER
static inline fn Image_setGpuDevice(Image *self, any v) { (*self).gpuDevice = v; }
;;SETTER
static inline fn Image_setGpuDescriptor(Image *self, any v) { (*self).gpuDescriptor = v; }
;;GETTER
static inline ImageGpuRefFn Image_gpuRetain(const Image *self) { return (*self).gpuRetain; }
;;SETTER
static inline fn Image_setGpuRetain(Image *self, ImageGpuRefFn v) { (*self).gpuRetain = v; }
;;GETTER
static inline ImageGpuRefFn Image_gpuRelease(const Image *self) { return (*self).gpuRelease; }
;;SETTER
static inline fn Image_setGpuRelease(Image *self, ImageGpuRefFn v) { (*self).gpuRelease = v; }

static fn clamp_dims(u32 *w, u32 *h) {
    if (*w == 0) *w = 1;
    if (*h == 0) *h = 1;
}

// Allocates image metadata and normalizes zero dimensions to one; pixel storage is lazy.
Image *Image_new(const ImageDesc *desc) {
    Image *img = calloc(1, sizeof *img);
    if (!img) return nullptr;
    ImageDesc d = desc ? *desc : (ImageDesc){1, 1, IMAGE_FORMAT_RGBA8, IMAGE_USAGE_NONE};
    clamp_dims(&d.width, &d.height);
    Image_setWidth(img, d.width);
    Image_setHeight(img, d.height);
    Image_setFormat(img, d.format);
    Image_setUsage(img, d.usage);
    return img;
}

// Creates an empty 1x1 RGBA image descriptor with no usage flags.
Image *Image_0() { return Image_new(nullptr); }
// Creates an RGBA image descriptor with the requested dimensions and no usage flags.
Image *Image_2(u32 width, u32 height) {
    ImageDesc d = {width, height, IMAGE_FORMAT_RGBA8, IMAGE_USAGE_NONE};
    return Image_new(&d);
}
// Creates an image descriptor with explicit dimensions, format, and usage.
Image *Image_4(u32 width, u32 height, u32 format, u32 usage) {
    ImageDesc d = {width, height, format, usage};
    return Image_new(&d);
}

// Releases GPU binding and owned CPU pixels, then frees metadata; a failed GPU release preserves the image.
fn Image_destroy(Image *image) {
    if (!image) return;
    if (!Image_clearGpu(image))
        return;
    free(Image_pixels(image));
    free(image);
}

// Ensures CPU RGBA shadow capacity, growing only when needed and updating logical extent.
bool Image_ensureShadow(Image *image, u32 width, u32 height) {
    if (!image) return false;
    clamp_dims(&width, &height);
    if ((u64) width * height > SIZE_MAX / 4u)
        return false;
    if (!Image_clearGpu(image))
        return false;
    // GROW-ONLY: reuse the existing allocation whenever it already fits, so a
    // shrinking viewport (or a resize wobble) never reallocs and never refills.
    if (Image_pixels(image) && width <= Image_capW(image) && height <= Image_capH(image)) {
        Image_setWidth(image, width);
        Image_setHeight(image, height);
        return true;
    }
    u32 nw = Image_capW(image) > width ? Image_capW(image) : width;
    u32 nh = Image_capH(image) > height ? Image_capH(image) : height;
    while (nw < width) nw += nw / 2 + 64;
    while (nh < height) nh += nh / 2 + 64;
    if ((u64) nw * nh > SIZE_MAX / 4u)
        return false;
    u8 *grown = realloc(Image_pixels(image), (bytes)nw * (bytes)nh * 4u);
    if (!grown) return false;
    Image_setPixels(image, grown);
    Image_setCapW(image, nw);
    Image_setCapH(image, nh);
    Image_setWidth(image, width);
    Image_setHeight(image, height);
    return true;
}

// Resizes the image's CPU shadow through the capacity-preserving ensure path.
bool Image_resize(Image *image, u32 width, u32 height) {
    return Image_ensureShadow(image, width, height);
}

// Copies tightly packed RGBA rows into the destination shadow, honoring its padded row stride.
bool Image_upload(const u8 *rgba, u32 width, u32 height, Image *dest) {
    if (!dest || !rgba) return false;
    if (!Image_ensureShadow(dest, width, height)) return false;
    bytes stride = Image_stride(dest);
    u8 *pixels = Image_pixels(dest);
    for (u32 y = 0; y < height; y++) {
        memcpy(pixels + (bytes)y * stride, rgba + (bytes)y * width * 4u, (bytes)width * 4u);
    }
    return true;
}

// Fills the current logical extent of the CPU shadow with one RGBA color.
fn Image_fill(Image *image, Color color) {
    if (!image) return;
    if (!Image_clearGpu(image))
        return;
    if (!Image_pixels(image) && !Image_ensureShadow(image, Image_width(image), Image_height(image))) return;
    u8 r = (u8)Color_red(color);
    u8 g = (u8)Color_green(color);
    u8 b = (u8)Color_blue(color);
    u8 a = (u8)Color_alpha(color);
    bytes stride = Image_stride(image);
    u8 *pixels = Image_pixels(image);
    u32 w = Image_width(image), h = Image_height(image);
    for (u32 y = 0; y < h; y++) {
        u8 *row = pixels + (bytes)y * stride;
        for (u32 x = 0; x < w; x++) {
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = a;
        }
    }
}

// Returns the logical image width, or zero when image is null.
u32 Image_width(const Image *image) { return image ? (*image).width : 0u; }
// Returns the logical image height, or zero when image is null.
u32 Image_height(const Image *image) { return image ? (*image).height : 0u; }
// Returns the declared pixel format, or the RGBA8 default for null.
u32 Image_format(const Image *image) { return image ? (*image).format : IMAGE_FORMAT_RGBA8; }
// Returns the declared usage flags, or IMAGE_USAGE_NONE for null.
u32 Image_usage(const Image *image) { return image ? (*image).usage : IMAGE_USAGE_NONE; }
// Returns allocated RGBA row stride in bytes, or zero for null.
bytes Image_stride(const Image *image) { return image ? (bytes)Image_capW(image) * 4u : 0u; }
// Returns the mutable CPU pixel buffer, or nullptr when image is null.
u8 *Image_pixels(const Image *image) { return image ? (*image).pixels : nullptr; }
// Reports whether the logical image extent is nonempty.
bool Image_isValid(const Image *image) { return image && Image_width(image) > 0 && Image_height(image) > 0; }

// Returns the borrowed opaque backend-native handle.
any Image_native(const Image *image) { return image ? (*image).native : nullptr; }
// Returns the borrowed opaque IOSurface handle.
;;GETTER
any Image_iosurface(const Image *image) { return image ? (*image).ioSurface : nullptr; }
// Stores a borrowed opaque backend-native handle without taking ownership.
fn Image_setNative(Image *image, any native) { if (image) (*image).native = native; }
// Stores a borrowed opaque IOSurface handle without taking ownership.
;;SETTER
fn Image_setIOSurface(Image *image, any ioSurface) { if (image) (*image).ioSurface = ioSurface; }
// Returns the image's atlas or sampler layer index.
u32 Image_layer(const Image *image) { return image ? (*image).layer : 0u; }
// Sets the image's atlas or sampler layer index.
fn Image_setLayer(Image *image, u32 layer) { if (image) (*image).layer = layer; }

// Returns the owned opaque GPU resource pointer.
any Image_gpuResource(const Image *image) { return image ? (*image).gpuResource : nullptr; }
// Returns the borrowed identity of the GPU device that owns the resource.
any Image_gpuDevice(const Image *image) { return image ? (*image).gpuDevice : nullptr; }
// Returns the borrowed GPU sampling descriptor.
any Image_gpuDescriptor(const Image *image) { return image ? (*image).gpuDescriptor : nullptr; }
// Reports whether the image has a valid extent and either CPU pixels or a GPU descriptor.
bool Image_isDrawable(const Image *image) {
    return Image_isValid(image) && (Image_pixels(image) || Image_gpuDescriptor(image));
}
// Releases the retained GPU resource; on release failure the existing binding remains intact.
bool Image_clearGpu(Image *image) {
    if (!image)
        return false;
    if (Image_gpuResource(image) && !Image_gpuRelease(image)(Image_gpuResource(image)))
        return false;
    Image_setGpuResource(image, nullptr);
    Image_setGpuDevice(image, nullptr);
    Image_setGpuDescriptor(image, nullptr);
    Image_setGpuRetain(image, nullptr);
    Image_setGpuRelease(image, nullptr);
    return true;
}
// Retains and installs a complete GPU binding, replacing the previous binding only after admission.
bool Image_bindGpu(Image *image, any resource, any device, any descriptor,
                   ImageGpuRefFn retain, ImageGpuRefFn release) {
    if (!image || !resource || !device || !descriptor || !retain || !release) {
        THROW("Image GPU binding requires a complete validated resource");
        return false;
    }
    if (Image_gpuResource(image) == resource && Image_gpuDevice(image) == device &&
        Image_gpuDescriptor(image) == descriptor && Image_gpuRetain(image) == retain && Image_gpuRelease(image) == release)
        return true;
    if (!retain(resource)) {
        THROW("Image GPU reference rejected");
        return false;
    }
    if (!Image_clearGpu(image)) {
        release(resource);
        return false;
    }
    Image_setGpuResource(image, resource);
    Image_setGpuDevice(image, device);
    Image_setGpuDescriptor(image, descriptor);
    Image_setGpuRetain(image, retain);
    Image_setGpuRelease(image, release);
    return true;
}
