#ifndef GRAPHVEX_SAMPLED_IMAGE_H
#define GRAPHVEX_SAMPLED_IMAGE_H
#include "vulkan/device.h"
#include "graphvex/lang/image.h"

typedef struct SampledImage SampledImage;
/* Owner-thread/external synchronization. Borrows Device, which must outlive all
 * images and frame references. _2 uploads a CPU RGBA shadow once; _5 adopts an
 * already-completed mutable UNORM/sRGB VkImage+memory in shader-read layout.
 * _5 takes ownership only on success. Handles stay opaque outside the driver.
 * _2 may return a pending object after a 100ms timeout; poll/release retry with
 * the same bound, never free upload resources under outstanding GPU work.
 * Byte-space UNORM sampling matches the existing UI presenter; scatter remains
 * linear-premultiplied. This is not a new linear-light UI compositor claim. */
SampledImage *SampledImage_0();
SampledImage *SampledImage_2(Device *device, const Image *source);
SampledImage *SampledImage_5(Device *device, any image, any memory, u32 width, u32 height);
#define GRAPHVEX_SAMPLED_CTOR(_0,_1,_2,_3,_4,_5,_6,NAME,...) NAME
#define SampledImage(...) GRAPHVEX_SAMPLED_CTOR(0 __VA_OPT__(,) __VA_ARGS__, \
    SampledImage_invalidArity,SampledImage_5,SampledImage_invalidArity,SampledImage_invalidArity, \
    SampledImage_2,SampledImage_invalidArity,SampledImage_0)(__VA_ARGS__)
SampledImage *SampledImage_zero();
bool SampledImage_poll(SampledImage *self);
bool SampledImage_retain(SampledImage *self);
/* A failed final release leaves the reference owned by the caller for retry. */
bool SampledImage_release(SampledImage *self);
bool SampledImage_isReady(const SampledImage *self);
bool SampledImage_bindImage(SampledImage *self, Image *image);
u32 SampledImage_width(const SampledImage *self);
u32 SampledImage_height(const SampledImage *self);
Device *SampledImage_device(const SampledImage *self);
any SampledImage_descriptor(const SampledImage *self);
fn SampledImage_toString(const SampledImage *self, char *dest, bytes cap, bool *outTruncated);
fn SampledImage_toStringStruct(const SampledImage *self, char *dest, bytes cap, bool *outTruncated);
#endif
