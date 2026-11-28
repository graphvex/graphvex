/**
 * io/image_io_cocoa.m -- macOS ImageIO runtime capability probe, RGBA8
 * decoder/encoder, and metadata-preserving file converter for graphvex/api/image.
 */

#define Rect MacOSQuickDrawRect
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#undef Rect

#include "vex.h"
#include "image/io/image_io.h"
#include "image/nio/image_memory.h"

#include <stdio.h>
#include <string.h>

;;PLATFORM_EXCLUSIVE("macOS")
;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ImageIOCocoa (graphvex/api/image/io/image_io_cocoa.m)
 * ============================================================================
 * Native macOS ImageIO backend for graphvex/api/image. Queries runtime readable
 * and writable UTIs via CGImageSourceCopyTypeIdentifiers and
 * CGImageDestinationCopyTypeIdentifiers, decodes any supported image format
 * (PNG, JPEG, HEIC/HEIF, AVIF, WebP, JXL, GIF, TIFF, BMP, TGA, DNG/RAW, PSD)
 * into straight-alpha sRGB RGBA8 inside Image_arena(), encodes RGBA8 buffers
 * atomically via CGImageDestination, and performs metadata-preserving
 * native-to-native file conversions.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ImageIOCocoa (io/image_io_cocoa.m)
 * ============================================================================
 * macOS ImageIO capability discovery, decode, encode, and file conversion.
 *
 * --FUNCTIONS----------------------------
 * public   ImageIO_canReadExt            checks runtime CGImageSource UTI list
 * public   ImageIO_canWriteExt           checks runtime CGImageDestination list
 * public   ImageIO_canReadFormat         checks runtime read support by enum
 * public   ImageIO_canWriteFormat        checks runtime write support by enum
 * public   ImageIO_probe                 probes dimensions/frames/orientation
 * public   ImageIO_loadRgba8             decodes file to straight RGBA8
 * public   ImageIO_freeRgba8             frees RGBA8 buffer from Image_arena
 * public   ImageIO_writeRgba8            encodes RGBA8 atomically to disk
 * public   ImageIO_convertFileNative     converts image file preserving EXIF/ICC
 *
 * --GETTERS------------------------------
 * private  imageOptsQuality              reads quality from ImageWriteOptions
 * private  imageOptsMaxDim               reads maxDimension from ImageWriteOptions
 * private  imageOptsBgRgba               reads opaqueBgRGBA from ImageWriteOptions
 * private  imageOptsPreserveMeta         reads preserveMetadata flag
 * private  imageMetaWidth                reads width from ImageMetadata
 * private  imageMetaHeight               reads height from ImageMetadata
 *
 * --SETTERS------------------------------
 * private  imageMetaSetFields            writes probed fields to ImageMetadata
 * ============================================================================
 */

;;GETTER
static f32 imageOptsQuality(const ImageWriteOptions *opts)
{
    if (!opts)
        return 0.90f;

    return (*opts).quality;
}

;;GETTER
static u32 imageOptsMaxDim(const ImageWriteOptions *opts)
{
    if (!opts)
        return 0u;

    return (*opts).maxDimension;
}

;;GETTER
static u32 imageOptsBgRgba(const ImageWriteOptions *opts)
{
    if (!opts)
        return 0x000000FFu;

    return (*opts).opaqueBgRGBA;
}

;;GETTER
static bool imageOptsPreserveMeta(const ImageWriteOptions *opts)
{
    if (!opts)
        return true;

    return (*opts).preserveMetadata;
}

;;GETTER
static u32 imageMetaWidth(const ImageMetadata *meta)
{
    if (!meta)
        return 0u;

    return (*meta).width;
}

;;GETTER
static u32 imageMetaHeight(const ImageMetadata *meta)
{
    if (!meta)
        return 0u;

    return (*meta).height;
}

;;SETTER
static fn imageMetaSetFields(ImageMetadata *meta, u32 w, u32 h, u32 frames,
                             u32 orientation, ImageFormat fmt, bool hasAlpha)
{
    if (!meta)
        return;

    (*meta).width = w;
    (*meta).height = h;
    (*meta).frameCount = frames;
    (*meta).orientation = orientation;
    (*meta).format = fmt;
    (*meta).hasAlpha = hasAlpha;
}

static CFStringRef imageCocoaUtiFromExt(string ext)
{
    if (!ext || ext[0] == '\0')
        return nullptr;

    ImageFormat fmt = ImageIO_formatFromExt(ext);
    string knownUti = ImageIO_formatUti(fmt);
    if (knownUti && knownUti[0] != '\0')
        return CFStringCreateWithCString(kCFAllocatorDefault, knownUti, kCFStringEncodingUTF8);

    id utTypeClass = NSClassFromString(@"UTType");
    if (!utTypeClass)
        return nullptr;

    SEL sel = NSSelectorFromString(@"typeWithFilenameExtension:");
    if (![utTypeClass respondsToSelector:sel])
        return nullptr;

    NSString *nsExt = [NSString stringWithUTF8String:ext];
    if (!nsExt)
        return nullptr;

    id utType = [utTypeClass performSelector:sel withObject:nsExt];
    if (!utType || ![utType respondsToSelector:@selector(identifier)])
        return nullptr;

    NSString *ident = [utType performSelector:@selector(identifier)];
    if (!ident)
        return nullptr;

    return CFStringCreateCopy(kCFAllocatorDefault, (__bridge CFStringRef) ident);
}

static bool imageCocoaArrayContainsUti(CFArrayRef arr, CFStringRef uti)
{
    if (!arr || !uti)
        return false;

    CFIndex count = CFArrayGetCount(arr);
    for (CFIndex i = 0; i < count; i++) {
        CFStringRef item = (CFStringRef) CFArrayGetValueAtIndex(arr, i);
        if (item && CFStringCompare(item, uti, kCFCompareCaseInsensitive) == kCFCompareEqualTo)
            return true;
    }
    return false;
}

static bool imageCocoaFormatIsOpaque(ImageFormat fmt)
{
    return fmt == IMAGE_FORMAT_JPEG || fmt == IMAGE_FORMAT_BMP;
}

static bool imageCocoaRasterizeCGImage(CGImageRef cgImg, u32 maxDimension,
                                       u8 **outPixels, u32 *outW, u32 *outH)
{
    if (!cgImg || !outPixels || !outW || !outH)
        return false;

    u32 srcW = (u32) CGImageGetWidth(cgImg);
    u32 srcH = (u32) CGImageGetHeight(cgImg);
    if (srcW == 0u || srcH == 0u)
        return false;

    u32 dstW = srcW;
    u32 dstH = srcH;
    if (maxDimension > 0u && (srcW > maxDimension || srcH > maxDimension)) {
        if (srcW >= srcH) {
            dstW = maxDimension;
            dstH = (u32) (((u64) srcH * (u64) maxDimension) / (u64) srcW);
        } else {
            dstH = maxDimension;
            dstW = (u32) (((u64) srcW * (u64) maxDimension) / (u64) srcH);
        }
        if (dstW == 0u)
            dstW = 1u;
        if (dstH == 0u)
            dstH = 1u;
    }

    bytes totalBytes = (bytes) dstW * (bytes) dstH * 4u;
    u8 *buf = (u8*) MemoryArena_alloc(Image_arena(), 0, totalBytes);
    if (!buf)
        return false;

    memset(buf, 0, totalBytes);

    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!cs)
        cs = CGColorSpaceCreateDeviceRGB();

    CGBitmapInfo info = (CGBitmapInfo) kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big;
    CGContextRef ctx = CGBitmapContextCreate(buf, dstW, dstH, 8, (bytes) dstW * 4u, cs, info);
    if (cs)
        CGColorSpaceRelease(cs);

    if (!ctx) {
        MemoryArena_free(Image_arena(), buf);
        return false;
    }

    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0.0, 0.0, (CGFloat) dstW, (CGFloat) dstH), cgImg);
    CGContextRelease(ctx);

    u32 pxCount = dstW * dstH;
    for (u32 i = 0u; i < pxCount; i++) {
        bytes off = (bytes) i * 4u;
        u8 a = buf[off + 3u];
        if (a > 0u && a < 255u) {
            u32 r = ((u32) buf[off + 0u] * 255u + (u32) (a / 2u)) / (u32) a;
            u32 g = ((u32) buf[off + 1u] * 255u + (u32) (a / 2u)) / (u32) a;
            u32 b = ((u32) buf[off + 2u] * 255u + (u32) (a / 2u)) / (u32) a;
            buf[off + 0u] = r > 255u ? 255u : (u8) r;
            buf[off + 1u] = g > 255u ? 255u : (u8) g;
            buf[off + 2u] = b > 255u ? 255u : (u8) b;
        }
    }

    *outPixels = buf;
    *outW = dstW;
    *outH = dstH;
    return true;
}

bool ImageIO_canReadExt(string ext)
{
    @autoreleasepool {
        CFStringRef uti = imageCocoaUtiFromExt(ext);
        if (!uti)
            return false;

        CFArrayRef types = CGImageSourceCopyTypeIdentifiers();
        bool ok = imageCocoaArrayContainsUti(types, uti);
        if (types)
            CFRelease(types);
        CFRelease(uti);
        return ok;
    }
}

bool ImageIO_canWriteExt(string ext)
{
    @autoreleasepool {
        CFStringRef uti = imageCocoaUtiFromExt(ext);
        if (!uti)
            return false;

        CFArrayRef types = CGImageDestinationCopyTypeIdentifiers();
        bool ok = imageCocoaArrayContainsUti(types, uti);
        if (types)
            CFRelease(types);
        CFRelease(uti);
        return ok;
    }
}

bool ImageIO_canReadFormat(ImageFormat fmt)
{
    return ImageIO_canReadExt(ImageIO_formatExtension(fmt));
}

bool ImageIO_canWriteFormat(ImageFormat fmt)
{
    return ImageIO_canWriteExt(ImageIO_formatExtension(fmt));
}

bool ImageIO_probe(string path, ImageMetadata *outMeta)
{
    if (!path || path[0] == '\0' || !outMeta)
        return false;

    memset(outMeta, 0, sizeof(ImageMetadata));

    @autoreleasepool {
        CFStringRef cfPath = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
        if (!cfPath)
            return false;

        CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfPath, kCFURLPOSIXPathStyle, false);
        CFRelease(cfPath);
        if (!url)
            return false;

        CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
        CFRelease(url);
        if (!src)
            return false;

        u32 count = (u32) CGImageSourceGetCount(src);
        if (count == 0u) {
            CFRelease(src);
            return false;
        }

        u32 w = 0u;
        u32 h = 0u;
        u32 orientation = 1u;
        bool hasAlpha = false;
        ImageFormat fmt = ImageIO_formatFromPath(path);

        CFDictionaryRef props = CGImageSourceCopyPropertiesAtIndex(src, 0, nullptr);
        if (props) {
            CFNumberRef pw = (CFNumberRef) CFDictionaryGetValue(props, kCGImagePropertyPixelWidth);
            CFNumberRef ph = (CFNumberRef) CFDictionaryGetValue(props, kCGImagePropertyPixelHeight);
            CFNumberRef ori = (CFNumberRef) CFDictionaryGetValue(props, kCGImagePropertyOrientation);
            CFBooleanRef alpha = (CFBooleanRef) CFDictionaryGetValue(props, kCGImagePropertyHasAlpha);
            i32 wVal = 0;
            i32 hVal = 0;
            i32 oVal = 1;
            if (pw && CFNumberGetValue(pw, kCFNumberSInt32Type, &wVal) && wVal > 0)
                w = (u32) wVal;
            if (ph && CFNumberGetValue(ph, kCFNumberSInt32Type, &hVal) && hVal > 0)
                h = (u32) hVal;
            if (ori && CFNumberGetValue(ori, kCFNumberSInt32Type, &oVal) && oVal >= 1 && oVal <= 8)
                orientation = (u32) oVal;
            if (alpha && CFGetTypeID(alpha) == CFBooleanGetTypeID())
                hasAlpha = CFBooleanGetValue(alpha) ? true : false;
            CFRelease(props);
        }

        CFRelease(src);
        imageMetaSetFields(outMeta, w, h, count, orientation, fmt, hasAlpha);
        return imageMetaWidth(outMeta) > 0u && imageMetaHeight(outMeta) > 0u;
    }
}

bool ImageIO_loadRgba8(string path, u32 maxDimension,
                       u8 **outPixels, u32 *outWidth, u32 *outHeight)
{
    if (!path || path[0] == '\0' || !outPixels || !outWidth || !outHeight)
        return false;

    *outPixels = nullptr;
    *outWidth = 0u;
    *outHeight = 0u;

    @autoreleasepool {
        CFStringRef cfPath = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
        if (!cfPath)
            return false;

        CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfPath, kCFURLPOSIXPathStyle, false);
        CFRelease(cfPath);
        if (!url)
            return false;

        CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
        CFRelease(url);
        if (!src)
            return false;

        CGImageRef cgImg = nullptr;
        if (maxDimension > 0u) {
            NSDictionary *thumbOpts = @{
                (id) kCGImageSourceCreateThumbnailFromImageAlways: @YES,
                (id) kCGImageSourceCreateThumbnailWithTransform: @YES,
                (id) kCGImageSourceThumbnailMaxPixelSize: @(maxDimension),
                (id) kCGImageSourceShouldCacheImmediately: @YES,
            };
            cgImg = CGImageSourceCreateThumbnailAtIndex(src, 0, (__bridge CFDictionaryRef) thumbOpts);
        }
        if (!cgImg)
            cgImg = CGImageSourceCreateImageAtIndex(src, 0, nullptr);

        CFRelease(src);
        if (!cgImg)
            return false;

        bool ok = imageCocoaRasterizeCGImage(cgImg, maxDimension, outPixels, outWidth, outHeight);
        CGImageRelease(cgImg);
        return ok;
    }
}

fn ImageIO_freeRgba8(u8 *pixels)
{
    if (pixels)
        MemoryArena_free(Image_arena(), pixels);
}

bool ImageIO_writeRgba8(string dstPath, const u8 *pixels, u32 width, u32 height,
                        u32 strideBytes, ImageFormat fmt, const ImageWriteOptions *opts)
{
    if (!dstPath || dstPath[0] == '\0' || !pixels || width == 0u || height == 0u)
        return false;

    ImageWriteOptions cfg = opts ? *opts : ImageWriteOptions_default();
    if (!ImageIO_canWriteFormat(fmt))
        return false;

    u32 srcStride = strideBytes >= width * 4u ? strideBytes : width * 4u;
    bool opaque = imageCocoaFormatIsOpaque(fmt);
    bytes tightBytes = (bytes) width * (bytes) height * 4u;
    u8 *stage = (u8*) MemoryArena_alloc(Image_arena(), 0, tightBytes);
    if (!stage)
        return false;

    u32 bgRgba = imageOptsBgRgba(&cfg);
    u8 bgR = (u8) ((bgRgba >> 24) & 0xFFu);
    u8 bgG = (u8) ((bgRgba >> 16) & 0xFFu);
    u8 bgB = (u8) ((bgRgba >> 8) & 0xFFu);

    for (u32 y = 0u; y < height; y++) {
        const u8 *srcRow = pixels + (bytes) y * (bytes) srcStride;
        u8 *dstRow = stage + (bytes) y * (bytes) width * 4u;
        for (u32 x = 0u; x < width; x++) {
            bytes off = (bytes) x * 4u;
            u8 r = srcRow[off + 0u];
            u8 g = srcRow[off + 1u];
            u8 b = srcRow[off + 2u];
            u8 a = srcRow[off + 3u];
            if (opaque) {
                u32 inv = 255u - (u32) a;
                dstRow[off + 0u] = (u8) (((u32) r * (u32) a + (u32) bgR * inv + 127u) / 255u);
                dstRow[off + 1u] = (u8) (((u32) g * (u32) a + (u32) bgG * inv + 127u) / 255u);
                dstRow[off + 2u] = (u8) (((u32) b * (u32) a + (u32) bgB * inv + 127u) / 255u);
                dstRow[off + 3u] = 255u;
            } else {
                dstRow[off + 0u] = (u8) (((u32) r * (u32) a + 127u) / 255u);
                dstRow[off + 1u] = (u8) (((u32) g * (u32) a + 127u) / 255u);
                dstRow[off + 2u] = (u8) (((u32) b * (u32) a + 127u) / 255u);
                dstRow[off + 3u] = a;
            }
        }
    }

    bool ok = false;
    @autoreleasepool {
        utf8 tmpPath[1024];
        snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", dstPath);

        CFStringRef uti = imageCocoaUtiFromExt(ImageIO_formatExtension(fmt));
        CFStringRef cfTmp = CFStringCreateWithCString(kCFAllocatorDefault, tmpPath, kCFStringEncodingUTF8);
        CFURLRef url = cfTmp ? CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfTmp, kCFURLPOSIXPathStyle, false) : nullptr;
        if (cfTmp)
            CFRelease(cfTmp);

        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        if (!cs)
            cs = CGColorSpaceCreateDeviceRGB();

        CGBitmapInfo binfo = (opaque ? (CGBitmapInfo) kCGImageAlphaNoneSkipLast
                                     : (CGBitmapInfo) kCGImageAlphaPremultipliedLast)
                             | kCGBitmapByteOrder32Big;
        CGContextRef ctx = CGBitmapContextCreate(stage, width, height, 8, (bytes) width * 4u, cs, binfo);
        if (cs)
            CGColorSpaceRelease(cs);

        CGImageRef cgImg = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
        if (ctx)
            CGContextRelease(ctx);

        CGImageDestinationRef dst = (url && uti && cgImg)
            ? CGImageDestinationCreateWithURL(url, uti, 1, nullptr)
            : nullptr;

        if (dst) {
            f32 q = imageOptsQuality(&cfg);
            if (q < 0.0f)
                q = 0.0f;
            if (q > 1.0f)
                q = 1.0f;
            NSDictionary *props = @{
                (id) kCGImageDestinationLossyCompressionQuality: @(q)
            };
            CGImageDestinationAddImage(dst, cgImg, (__bridge CFDictionaryRef) props);
            ok = CGImageDestinationFinalize(dst) ? true : false;
            CFRelease(dst);
        }

        if (cgImg)
            CGImageRelease(cgImg);
        if (url)
            CFRelease(url);
        if (uti)
            CFRelease(uti);

        if (ok) {
            if (rename(tmpPath, dstPath) != 0) {
                remove(tmpPath);
                ok = false;
            }
        } else {
            remove(tmpPath);
        }
    }

    MemoryArena_free(Image_arena(), stage);
    return ok;
}

bool ImageIO_convertFileNative(string srcPath, string dstPath,
                               ImageFormat dstFormat, const ImageWriteOptions *opts)
{
    if (!srcPath || srcPath[0] == '\0' || !dstPath || dstPath[0] == '\0')
        return false;

    ImageWriteOptions cfg = opts ? *opts : ImageWriteOptions_default();
    if (!ImageIO_canWriteFormat(dstFormat))
        return false;

    u32 maxDim = imageOptsMaxDim(&cfg);
    bool keepMeta = imageOptsPreserveMeta(&cfg);
    f32 q = imageOptsQuality(&cfg);
    if (q < 0.0f)
        q = 0.0f;
    if (q > 1.0f)
        q = 1.0f;

    @autoreleasepool {
        CFStringRef cfSrc = CFStringCreateWithCString(kCFAllocatorDefault, srcPath, kCFStringEncodingUTF8);
        CFURLRef srcUrl = cfSrc ? CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfSrc, kCFURLPOSIXPathStyle, false) : nullptr;
        if (cfSrc)
            CFRelease(cfSrc);
        if (!srcUrl)
            return false;

        CGImageSourceRef src = CGImageSourceCreateWithURL(srcUrl, nullptr);
        CFRelease(srcUrl);
        if (!src)
            return false;

        u32 count = (u32) CGImageSourceGetCount(src);
        if (count == 0u) {
            CFRelease(src);
            return false;
        }

        bool multiFrame = (count > 1u) && (dstFormat == IMAGE_FORMAT_GIF || dstFormat == IMAGE_FORMAT_TIFF);
        u32 outCount = multiFrame ? count : 1u;

        utf8 tmpPath[1024];
        snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", dstPath);

        CFStringRef uti = imageCocoaUtiFromExt(ImageIO_formatExtension(dstFormat));
        CFStringRef cfTmp = CFStringCreateWithCString(kCFAllocatorDefault, tmpPath, kCFStringEncodingUTF8);
        CFURLRef dstUrl = cfTmp ? CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfTmp, kCFURLPOSIXPathStyle, false) : nullptr;
        if (cfTmp)
            CFRelease(cfTmp);

        CGImageDestinationRef dst = (dstUrl && uti)
            ? CGImageDestinationCreateWithURL(dstUrl, uti, outCount, nullptr)
            : nullptr;

        if (dstUrl)
            CFRelease(dstUrl);
        if (uti)
            CFRelease(uti);

        if (!dst) {
            CFRelease(src);
            return false;
        }

        for (u32 i = 0u; i < outCount; i++) {
            CGImageRef frame = nullptr;
            if (maxDim > 0u) {
                NSDictionary *thumbOpts = @{
                    (id) kCGImageSourceCreateThumbnailFromImageAlways: @YES,
                    (id) kCGImageSourceCreateThumbnailWithTransform: @YES,
                    (id) kCGImageSourceThumbnailMaxPixelSize: @(maxDim),
                };
                frame = CGImageSourceCreateThumbnailAtIndex(src, i, (__bridge CFDictionaryRef) thumbOpts);
            }
            if (!frame)
                frame = CGImageSourceCreateImageAtIndex(src, i, nullptr);
            if (!frame)
                continue;

            NSMutableDictionary *frameProps = [NSMutableDictionary dictionary];
            if (keepMeta) {
                CFDictionaryRef srcProps = CGImageSourceCopyPropertiesAtIndex(src, i, nullptr);
                if (srcProps) {
                    [frameProps addEntriesFromDictionary:(__bridge NSDictionary *) srcProps];
                    CFRelease(srcProps);
                }
                if (maxDim > 0u)
                    [frameProps removeObjectForKey:(id) kCGImagePropertyOrientation];
            }
            [frameProps setObject:@(q) forKey:(id) kCGImageDestinationLossyCompressionQuality];
            CGImageDestinationAddImage(dst, frame, (__bridge CFDictionaryRef) frameProps);
            CGImageRelease(frame);
        }

        bool ok = CGImageDestinationFinalize(dst) ? true : false;
        CFRelease(dst);
        CFRelease(src);

        if (ok) {
            if (rename(tmpPath, dstPath) != 0) {
                remove(tmpPath);
                ok = false;
            }
        } else {
            remove(tmpPath);
        }
        return ok;
    }
}
