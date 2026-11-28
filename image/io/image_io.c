#include "vex.h"
#include "image/io/image_io.h"
#include "image/nio/image_memory.h"

#include <ctype.h>
#include <string.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ImageIO (graphvex/api/image/io/image_io.c)
 * ============================================================================
 * Platform-neutral image format dispatch, GraphicsImage currency upload and
 * readback bridge, and file conversion routing between the native OS ImageIO
 * backend and an optional caller-supplied ImageConvertHook.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ImageIO (io/image_io.c)
 * ============================================================================
 * Format helpers, GraphicsImage load/save currency bridge, and convert router.
 *
 * --FUNCTIONS----------------------------
 * public   ImageWriteOptions_default    returns default quality/metadata opts
 * public   ImageIO_formatFromExt        maps extension string to ImageFormat
 * public   ImageIO_formatFromPath       detects ImageFormat from file path
 * public   ImageIO_formatExtension      returns canonical lowercase extension
 * public   ImageIO_formatUti            returns Apple/canonical UTI string
 * public   ImageIO_loadGraphicsImage    decodes disk image into GraphicsImage
 * public   ImageIO_saveGraphicsImage    encodes GraphicsImage to disk
 * public   ImageIO_convertFile          routes native convert or external hook
 *
 * --GETTERS------------------------------
 * private  imageHookCanHandle           reads hook canHandle callback
 * private  imageHookConvert             reads hook convert callback
 * private  imageHookContext             reads hook context pointer
 *
 * --SETTERS------------------------------
 * private  imageOptsInitDefaults        initializes default ImageWriteOptions
 * ============================================================================
 */

;;SETTER
static fn imageOptsInitDefaults(ImageWriteOptions *opts)
{
    if (!opts)
        return;

    (*opts).quality = 0.90f;
    (*opts).maxDimension = 0u;
    (*opts).opaqueBgRGBA = 0x000000FFu;
    (*opts).preserveMetadata = true;
}

;;GETTER
static ImageConvertHookCanHandleFn imageHookCanHandle(const ImageConvertHook *h)
{
    if (!h)
        return nullptr;

    return (*h).canHandle;
}

;;GETTER
static ImageConvertHookConvertFn imageHookConvert(const ImageConvertHook *h)
{
    if (!h)
        return nullptr;

    return (*h).convert;
}

;;GETTER
static any imageHookContext(const ImageConvertHook *h)
{
    if (!h)
        return nullptr;

    return (*h).context;
}

static bool imageExtEqualsIgnoreCase(string a, string b)
{
    if (!a || !b)
        return false;

    while (*a != '\0' && *b != '\0') {
        i32 ca = tolower((u8) *a);
        i32 cb = tolower((u8) *b);
        if (ca != cb)
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

ImageWriteOptions ImageWriteOptions_default()
{
    ImageWriteOptions opts;
    memset(&opts, 0, sizeof(ImageWriteOptions));
    imageOptsInitDefaults(&opts);
    return opts;
}

ImageFormat ImageIO_formatFromExt(string ext)
{
    if (!ext || ext[0] == '\0')
        return IMAGE_FORMAT_UNKNOWN;

    while (*ext == '.' || *ext == ' ')
        ext++;

    if (imageExtEqualsIgnoreCase(ext, "png"))
        return IMAGE_FORMAT_PNG;
    if (imageExtEqualsIgnoreCase(ext, "jpg") || imageExtEqualsIgnoreCase(ext, "jpeg"))
        return IMAGE_FORMAT_JPEG;
    if (imageExtEqualsIgnoreCase(ext, "heic"))
        return IMAGE_FORMAT_HEIC;
    if (imageExtEqualsIgnoreCase(ext, "heif") || imageExtEqualsIgnoreCase(ext, "hif"))
        return IMAGE_FORMAT_HEIF;
    if (imageExtEqualsIgnoreCase(ext, "avif"))
        return IMAGE_FORMAT_AVIF;
    if (imageExtEqualsIgnoreCase(ext, "webp"))
        return IMAGE_FORMAT_WEBP;
    if (imageExtEqualsIgnoreCase(ext, "jxl"))
        return IMAGE_FORMAT_JXL;
    if (imageExtEqualsIgnoreCase(ext, "gif"))
        return IMAGE_FORMAT_GIF;
    if (imageExtEqualsIgnoreCase(ext, "tif") || imageExtEqualsIgnoreCase(ext, "tiff"))
        return IMAGE_FORMAT_TIFF;
    if (imageExtEqualsIgnoreCase(ext, "bmp"))
        return IMAGE_FORMAT_BMP;
    if (imageExtEqualsIgnoreCase(ext, "tga"))
        return IMAGE_FORMAT_TGA;
    if (imageExtEqualsIgnoreCase(ext, "dng"))
        return IMAGE_FORMAT_DNG;
    if (imageExtEqualsIgnoreCase(ext, "psd"))
        return IMAGE_FORMAT_PSD;

    return IMAGE_FORMAT_UNKNOWN;
}

ImageFormat ImageIO_formatFromPath(string path)
{
    if (!path || path[0] == '\0')
        return IMAGE_FORMAT_UNKNOWN;

    string dot = nullptr;
    string p = path;
    while (*p != '\0') {
        if (*p == '/' || *p == '\\')
            dot = nullptr;
        else if (*p == '.')
            dot = p + 1;
        p++;
    }
    if (!dot || dot[0] == '\0')
        return IMAGE_FORMAT_UNKNOWN;

    return ImageIO_formatFromExt(dot);
}

string ImageIO_formatExtension(ImageFormat fmt)
{
    switch (fmt) {
        case IMAGE_FORMAT_PNG:  return "png";
        case IMAGE_FORMAT_JPEG: return "jpg";
        case IMAGE_FORMAT_HEIC: return "heic";
        case IMAGE_FORMAT_HEIF: return "heif";
        case IMAGE_FORMAT_AVIF: return "avif";
        case IMAGE_FORMAT_WEBP: return "webp";
        case IMAGE_FORMAT_JXL:  return "jxl";
        case IMAGE_FORMAT_GIF:  return "gif";
        case IMAGE_FORMAT_TIFF: return "tiff";
        case IMAGE_FORMAT_BMP:  return "bmp";
        case IMAGE_FORMAT_TGA:  return "tga";
        case IMAGE_FORMAT_DNG:  return "dng";
        case IMAGE_FORMAT_PSD:  return "psd";
        default:                return "";
    }
}

string ImageIO_formatUti(ImageFormat fmt)
{
    switch (fmt) {
        case IMAGE_FORMAT_PNG:  return "public.png";
        case IMAGE_FORMAT_JPEG: return "public.jpeg";
        case IMAGE_FORMAT_HEIC: return "public.heic";
        case IMAGE_FORMAT_HEIF: return "public.heif";
        case IMAGE_FORMAT_AVIF: return "public.avif";
        case IMAGE_FORMAT_WEBP: return "org.webmproject.webp";
        case IMAGE_FORMAT_JXL:  return "public.jpeg-xl";
        case IMAGE_FORMAT_GIF:  return "com.compuserve.gif";
        case IMAGE_FORMAT_TIFF: return "public.tiff";
        case IMAGE_FORMAT_BMP:  return "com.microsoft.bmp";
        case IMAGE_FORMAT_TGA:  return "com.truevision.tga-image";
        case IMAGE_FORMAT_DNG:  return "com.adobe.raw-image";
        case IMAGE_FORMAT_PSD:  return "com.adobe.photoshop-image";
        default:                return "";
    }
}

GraphicsImage ImageIO_loadGraphicsImage(Graphics *g, ImageArena *arena,
                                        string path, u32 maxDimension)
{
    if (!g || !arena || !path || path[0] == '\0')
        return GraphicsImage();

    u8 *pixels = nullptr;
    u32 w = 0u;
    u32 h = 0u;
    if (!ImageIO_loadRgba8(path, maxDimension, &pixels, &w, &h) || !pixels || w == 0u || h == 0u) {
        ImageIO_freeRgba8(pixels);
        return GraphicsImage();
    }

    GraphicsImage img = GraphicsImage(g, arena, w, h, PIXEL_FORMAT_RGBA8_UNORM);
    if (!GraphicsImage_isValid(&img)) {
        ImageIO_freeRgba8(pixels);
        return GraphicsImage();
    }

    const ImagePool *pool = ImageArena_pool(arena);
    if (!pool)
        pool = Graphics_pool(g);

    u32 dstStride = GraphicsImage_strideBytes(&img);
    bytes rowBytes = (bytes) w * 4u;
    u8 *mapped = (u8*) ImageMemory_map(pool, GraphicsImage_data(&img));
    if (mapped) {
        for (u32 y = 0u; y < h; y++) {
            memcpy(mapped + (bytes) y * (bytes) dstStride,
                   pixels + (bytes) y * rowBytes,
                   rowBytes);
        }
    } else {
        vpointer dstAddr = GraphicsImage_data(&img);
        for (u32 y = 0u; y < h; y++) {
            ImageMemory_write(pool,
                              dstAddr + (u64) y * (u64) dstStride,
                              pixels + (bytes) y * rowBytes,
                              rowBytes);
        }
    }

    ImageIO_freeRgba8(pixels);
    return img;
}

bool ImageIO_saveGraphicsImage(Graphics *g, const GraphicsImage *img,
                               string dstPath, ImageFormat fmt,
                               const ImageWriteOptions *opts)
{
    if (!g || !img || !GraphicsImage_isValid(img) || !dstPath || dstPath[0] == '\0')
        return false;

    ImageFormat targetFmt = fmt;
    if (targetFmt == IMAGE_FORMAT_UNKNOWN)
        targetFmt = ImageIO_formatFromPath(dstPath);

    if (targetFmt == IMAGE_FORMAT_UNKNOWN || !ImageIO_canWriteFormat(targetFmt))
        return false;

    u32 w = GraphicsImage_width(img);
    u32 h = GraphicsImage_height(img);
    u32 stride = GraphicsImage_strideBytes(img);
    if (w == 0u || h == 0u || stride < w * 4u)
        return false;

    const ImagePool *pool = Graphics_resolvePool(g, GraphicsImage_data(img), GraphicsImage_byteSize(img));
    if (!pool)
        pool = Graphics_pool(g);

    const u8 *mapped = (const u8*) ImageMemory_map(pool, GraphicsImage_data(img));
    if (mapped)
        return ImageIO_writeRgba8(dstPath, mapped, w, h, stride, targetFmt, opts);

    bytes rowBytes = (bytes) w * 4u;
    bytes totalBytes = rowBytes * (bytes) h;
    u8 *readback = (u8*) MemoryArena_alloc(Image_arena(), 0, totalBytes);
    if (!readback)
        return false;

    vpointer srcAddr = GraphicsImage_data(img);
    bool readOk = true;
    for (u32 y = 0u; y < h; y++) {
        if (!ImageMemory_read(pool,
                              readback + (bytes) y * rowBytes,
                              srcAddr + (u64) y * (u64) stride,
                              rowBytes)) {
            readOk = false;
            break;
        }
    }

    bool wrote = false;
    if (readOk)
        wrote = ImageIO_writeRgba8(dstPath, readback, w, h, (u32) rowBytes, targetFmt, opts);

    MemoryArena_free(Image_arena(), readback);
    return wrote;
}

bool ImageIO_saveImage(Graphics *g, const Image *img,
                       string dstPath, ImageFormat fmt,
                       const ImageWriteOptions *opts)
{
    if (!g || !img || !Image_isValid(img))
        return false;

    const GraphicsImage *gi = Image_asGraphicsConst(img);
    if (gi)
        return ImageIO_saveGraphicsImage(g, gi, dstPath, fmt, opts);

    return false;
}

bool ImageIO_convertFile(string srcPath, string dstPath,
                         ImageFormat dstFormat, const ImageWriteOptions *opts,
                         const ImageConvertHook *externalHook)
{
    if (!srcPath || srcPath[0] == '\0' || !dstPath || dstPath[0] == '\0')
        return false;

    ImageFormat targetFmt = dstFormat;
    if (targetFmt == IMAGE_FORMAT_UNKNOWN)
        targetFmt = ImageIO_formatFromPath(dstPath);
    if (targetFmt == IMAGE_FORMAT_UNKNOWN)
        return false;

    ImageFormat srcFmt = ImageIO_formatFromPath(srcPath);
    bool nativeRead = (srcFmt == IMAGE_FORMAT_UNKNOWN) ? true : ImageIO_canReadFormat(srcFmt);
    bool nativeWrite = ImageIO_canWriteFormat(targetFmt);
    if (nativeRead && nativeWrite) {
        if (ImageIO_convertFileNative(srcPath, dstPath, targetFmt, opts))
            return true;
    }

    ImageConvertHookCanHandleFn canFn = imageHookCanHandle(externalHook);
    ImageConvertHookConvertFn runFn = imageHookConvert(externalHook);
    if (canFn && runFn) {
        any ctx = imageHookContext(externalHook);
        if (canFn(ctx, srcPath, targetFmt))
            return runFn(ctx, srcPath, dstPath, targetFmt, opts);
    }

    return false;
}
