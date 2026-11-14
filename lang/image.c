#include "vex.h"
#include "graphvex/lang/image.h"

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

static fn clamp_dims(u32 *w, u32 *h) {
    if (*w == 0) *w = 1;
    if (*h == 0) *h = 1;
}

// Allocates image metadata and normalizes zero dimensions to one; pixel storage is lazy.
Image *Image_new(const ImageDesc *desc) {
    Image *img = calloc(1, sizeof *img);
    if (!img) return nullptr;
    (*img).width = desc ? (*desc).width : 1;
    (*img).height = desc ? (*desc).height : 1;
    (*img).format = desc ? (*desc).format : IMAGE_FORMAT_RGBA8;
    (*img).usage = desc ? (*desc).usage : IMAGE_USAGE_NONE;
    clamp_dims(&(*img).width, &(*img).height);
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
    free((*image).pixels);
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
    if ((*image).pixels && width <= (*image).capW && height <= (*image).capH) {
        (*image).width = width;
        (*image).height = height;
        return true;
    }
    u32 nw = (*image).capW > width ? (*image).capW : width;
    u32 nh = (*image).capH > height ? (*image).capH : height;
    while (nw < width) nw += nw / 2 + 64;
    while (nh < height) nh += nh / 2 + 64;
    if ((u64) nw * nh > SIZE_MAX / 4u)
        return false;
    u8 *grown = realloc((*image).pixels, (bytes)nw * (bytes)nh * 4u);
    if (!grown) return false;
    (*image).pixels = grown;
    (*image).capW = nw;
    (*image).capH = nh;
    (*image).width = width;
    (*image).height = height;
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
    bytes stride = (bytes)((*dest).capW) * 4u;
    for (u32 y = 0; y < height; y++) {
        memcpy((*dest).pixels + (bytes)y * stride, rgba + (bytes)y * width * 4u, (bytes)width * 4u);
    }
    return true;
}

// Fills the current logical extent of the CPU shadow with one RGBA color.
fn Image_fill(Image *image, Color color) {
    if (!image) return;
    if (!Image_clearGpu(image))
        return;
    if (!(*image).pixels && !Image_ensureShadow(image, (*image).width, (*image).height)) return;
    u8 r = (u8)Color_red(color);
    u8 g = (u8)Color_green(color);
    u8 b = (u8)Color_blue(color);
    u8 a = (u8)Color_alpha(color);
    bytes stride = (bytes)((*image).capW) * 4u;
    for (u32 y = 0; y < (*image).height; y++) {
        u8 *row = (*image).pixels + (bytes)y * stride;
        for (u32 x = 0; x < (*image).width; x++) {
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
bytes Image_stride(const Image *image) { return image ? (bytes)((*image).capW) * 4u : 0u; }
// Returns the mutable CPU pixel buffer, or nullptr when image is null.
u8 *Image_pixels(const Image *image) { return image ? (*image).pixels : nullptr; }
// Reports whether the logical image extent is nonempty.
bool Image_isValid(const Image *image) { return image && (*image).width > 0 && (*image).height > 0; }

// Returns the borrowed opaque backend-native handle.
any Image_native(const Image *image) { return image ? (*image).native : nullptr; }
// Returns the borrowed opaque IOSurface handle.
any Image_iosurface(const Image *image) { return image ? (*image).ioSurface : nullptr; }
// Stores a borrowed opaque backend-native handle without taking ownership.
fn Image_setNative(Image *image, any native) { if (image) (*image).native = native; }
// Stores a borrowed opaque IOSurface handle without taking ownership.
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
    if ((*image).gpuResource && !(*image).gpuRelease((*image).gpuResource))
        return false;
    (*image).gpuResource = nullptr;
    (*image).gpuDevice = nullptr;
    (*image).gpuDescriptor = nullptr;
    (*image).gpuRetain = nullptr;
    (*image).gpuRelease = nullptr;
    return true;
}
// Retains and installs a complete GPU binding, replacing the previous binding only after admission.
bool Image_bindGpu(Image *image, any resource, any device, any descriptor,
                   ImageGpuRefFn retain, ImageGpuRefFn release) {
    if (!image || !resource || !device || !descriptor || !retain || !release) {
        THROW("Image GPU binding requires a complete validated resource");
        return false;
    }
    if ((*image).gpuResource == resource && (*image).gpuDevice == device &&
        (*image).gpuDescriptor == descriptor && (*image).gpuRetain == retain && (*image).gpuRelease == release)
        return true;
    if (!retain(resource)) {
        THROW("Image GPU reference rejected");
        return false;
    }
    if (!Image_clearGpu(image)) {
        release(resource);
        return false;
    }
    (*image).gpuResource = resource;
    (*image).gpuDevice = device;
    (*image).gpuDescriptor = descriptor;
    (*image).gpuRetain = retain;
    (*image).gpuRelease = release;
    return true;
}
