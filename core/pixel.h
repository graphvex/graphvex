#ifndef GRAPHVEX_CORE_PIXEL_H
#define GRAPHVEX_CORE_PIXEL_H

/** core/pixel.h -- pixel formats, strict 0xRRGGBBAA color helpers, and core IDs. */

#include "vex.h"

/** Bounded Wait Law: 100 ms ceiling on every internal driver wait. */
#define FENCE_WAIT_TIMEOUT_NS 100'000'000ULL

/** Strict 0xRRGGBBAA Color Law constants. */
#define COLOR_CLEAR_RGBA 0x00000000u
#define COLOR_BLACK_RGBA 0x000000FFu
#define COLOR_WHITE_RGBA 0xFFFFFFFFu

/** Bindless slot 0 is reserved for the dummy 1x1 white image. */
#define GRAPHICS_BINDLESS_DUMMY_SLOT 0u
#define GRAPHICS_MAX_BINDINGS 4u

/** One Type Registry Law: graphvex api owns 0x0100 - 0x013F. */
#define ID_GRAPHICS         0x0100u
#define ID_GRAPHICS_IMAGE   0x0101u
#define ID_GRAPHICS_RECT    0x0102u
#define ID_GRAPHICS_COMMAND 0x0103u
#define ID_SHADER_SOURCE    0x0104u

;;CLASSNAME("PixelFormat")

typedef enum PixelFormat {
    PIXEL_FORMAT_RGBA8_UNORM     = 0,
    PIXEL_FORMAT_RGBA8_SRGB      = 1,
    PIXEL_FORMAT_RGBA16_FLOAT    = 2,
    PIXEL_FORMAT_RGBA32_FLOAT    = 3,
    PIXEL_FORMAT_RG16_FLOAT      = 4,
    PIXEL_FORMAT_R16_UNORM       = 5,
    PIXEL_FORMAT_R32_FLOAT       = 6,
    PIXEL_FORMAT_R32_UINT        = 7,
    PIXEL_FORMAT_D32_FLOAT       = 8,
    PIXEL_FORMAT_D32_FLOAT_S8    = 9,
    PIXEL_FORMAT_R8_UNORM        = 10,
    PIXEL_FORMAT_COUNT           = 11
} PixelFormat;

u32 PixelFormat_bytesPerPixel(PixelFormat format);

;;CLASSNAME("ColorRGBA")

static inline u8 ColorRGBA_red(u32 rgba)
{
    return (u8) ((rgba >> 24) & 0xFFu);
}
static inline u8 ColorRGBA_green(u32 rgba)
{
    return (u8) ((rgba >> 16) & 0xFFu);
}
static inline u8 ColorRGBA_blue(u32 rgba)
{
    return (u8) ((rgba >> 8)  & 0xFFu);
}
static inline u8 ColorRGBA_alpha(u32 rgba)
{
    return (u8) (rgba & 0xFFu);
}
static inline u32 ColorRGBA_pack(u8 r, u8 g, u8 b, u8 a) {
    return ((u32) r << 24) | ((u32) g << 16) | ((u32) b << 8) | (u32) a;
}

#endif
