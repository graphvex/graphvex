#ifndef GRAPHVEX_IMAGE_IO_H
#define GRAPHVEX_IMAGE_IO_H

/**
 * io/image_io.h -- OS-native image decoding, encoding, format probing,
 * GraphicsImage currency bridge, and optional external converter hook.
 */

#include "vex.h"
#include "core/graphics.h"
#include "core/image.h"

;;CLASSNAME("ImageFormat")

typedef enum ImageFormat {
    IMAGE_FORMAT_UNKNOWN = 0u,
    IMAGE_FORMAT_PNG     = 1u,
    IMAGE_FORMAT_JPEG    = 2u,
    IMAGE_FORMAT_HEIC    = 3u,
    IMAGE_FORMAT_HEIF    = 4u,
    IMAGE_FORMAT_AVIF    = 5u,
    IMAGE_FORMAT_WEBP    = 6u,
    IMAGE_FORMAT_JXL     = 7u,
    IMAGE_FORMAT_GIF     = 8u,
    IMAGE_FORMAT_TIFF    = 9u,
    IMAGE_FORMAT_BMP     = 10u,
    IMAGE_FORMAT_TGA     = 11u,
    IMAGE_FORMAT_DNG     = 12u,
    IMAGE_FORMAT_PSD     = 13u,
} ImageFormat;

;;CLASSNAME("ImageMetadata")

/**
 * Probed metadata for an image file on disk without decoding full pixel data.
 * width         pixel width of frame 0
 * height        pixel height of frame 0
 * frameCount    number of frames (greater than 1 for animated GIF/WebP/TIFF)
 * orientation   EXIF orientation tag (1..8, defaults to 1)
 * format        detected ImageFormat from extension or UTI
 * hasAlpha      true if the source image carries an alpha channel
 */
typedef struct ImageMetadata {
    u32 width;
    u32 height;
    u32 frameCount;
    u32 orientation;
    ImageFormat format;
    bool hasAlpha;
} ImageMetadata;

;;CLASSNAME("ImageWriteOptions")

/**
 * Encoding and conversion controls for ImageIO_saveGraphicsImage and
 * ImageIO_convertFile.
 * quality           lossy compression quality in [0.0, 1.0] (default 0.90)
 * maxDimension      optional long-edge resize cap in pixels (0 keeps original)
 * opaqueBgRGBA      packed RGBA8 background fill when dropping alpha (0x000000FF)
 * preserveMetadata  true to carry EXIF/ICC/orientation tags during file convert
 */
typedef struct ImageWriteOptions {
    f32 quality;
    u32 maxDimension;
    u32 opaqueBgRGBA;
    bool preserveMetadata;
} ImageWriteOptions;

typedef bool (*ImageConvertHookCanHandleFn)(any context, string srcPath, ImageFormat dstFormat);
typedef bool (*ImageConvertHookConvertFn)(any context, string srcPath, string dstPath,
                                          ImageFormat dstFormat, const ImageWriteOptions *opts);

;;CLASSNAME("ImageConvertHook")

/**
 * API-only optional external converter seam (zero third-party code vendored).
 * Callers may wire an external tool or library (such as ffmpeg or cwebp) at
 * runtime for formats the host OS cannot natively read or write.
 * name       human-readable hook identifier
 * context    opaque caller-owned state passed to callbacks
 * canHandle  returns true if this hook can convert srcPath to dstFormat
 * convert    performs the conversion to dstPath and returns true on success
 */
typedef struct ImageConvertHook {
    string name;
    any context;
    ImageConvertHookCanHandleFn canHandle;
    ImageConvertHookConvertFn convert;
} ImageConvertHook;

ImageWriteOptions ImageWriteOptions_default();

ImageFormat ImageIO_formatFromExt(string ext);
ImageFormat ImageIO_formatFromPath(string path);
string ImageIO_formatExtension(ImageFormat fmt);
string ImageIO_formatUti(ImageFormat fmt);

bool ImageIO_canReadExt(string ext);
bool ImageIO_canWriteExt(string ext);
bool ImageIO_canReadFormat(ImageFormat fmt);
bool ImageIO_canWriteFormat(ImageFormat fmt);
bool ImageIO_probe(string path, ImageMetadata *outMeta);

bool ImageIO_loadRgba8(string path, u32 maxDimension,
                       u8 **outPixels, u32 *outWidth, u32 *outHeight);
fn ImageIO_freeRgba8(u8 *pixels);
bool ImageIO_writeRgba8(string dstPath, const u8 *pixels, u32 width, u32 height,
                        u32 strideBytes, ImageFormat fmt, const ImageWriteOptions *opts);

GraphicsImage ImageIO_loadGraphicsImage(Graphics *g, ImageArena *arena,
                                        string path, u32 maxDimension);
bool ImageIO_saveGraphicsImage(Graphics *g, const GraphicsImage *img,
                               string dstPath, ImageFormat fmt,
                               const ImageWriteOptions *opts);
bool ImageIO_saveImage(Graphics *g, const Image *img,
                       string dstPath, ImageFormat fmt,
                       const ImageWriteOptions *opts);

bool ImageIO_convertFileNative(string srcPath, string dstPath,
                               ImageFormat dstFormat, const ImageWriteOptions *opts);
bool ImageIO_convertFile(string srcPath, string dstPath,
                         ImageFormat dstFormat, const ImageWriteOptions *opts,
                         const ImageConvertHook *externalHook);

#endif
