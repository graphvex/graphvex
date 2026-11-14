#include "decode/png.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "util/throw.h"
#include "util/annotation.h"

;;DEFINITION
/**
 * ============================================================================
 * MODULE: Png (image/png.c)
 * ============================================================================
 * A pure PNG → RGBA8 decoder: bytes in, pixels out. No file, no path, no handle;
 * the caller supplies the bytes (engine File owns that) and receives a
 * Memory_alloc-backed buffer — a stable storage pointer to free with Memory_free.
 * DEFLATE comes from system zlib. Interlaced, 16-bit, palette and sub-8-bit
 * streams are rejected rather than half-decoded, and every chunk's CRC is
 * verified so a corrupted stream never yields plausible pixels.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Png (image/png.c)
 * ============================================================================
 * NO STRUCT FIELDS — procedural. Owns the caller's decoded buffer, nothing else.
 *
 * PUBLIC FUNCTION REGISTRY:
 *   - bool Png_isPng(bytes, length)               : signature test
 *   - PngCode Png_decode(bytes, length, outRgba, outWidth, outHeight)
 *   - const char *Png_codeName(code)              : stable code name
 *
 * PRIVATE HELPERS:
 *   pngReadBE32(p)          : big-endian u32
 *   pngChannels(colorType)  : samples per pixel, 0 when unsupported
 *   pngUnfilter(...)        : in-place PNG filter reconstruction
 *   pngExpand(...)          : samples → RGBA8
 *
 * CONTRACT: null output pointers or a non-PNG stream reject with PNG_INVALID and
 * one THROW; truncated streams are PNG_TRUNCATED; unsupported forms are
 * PNG_UNSUPPORTED. On failure nothing is allocated or returned. Geometry is
 * bounded before allocation so a hostile header cannot exhaust the arena.
 * ============================================================================
 */

static const u8 PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
static const u32 PNG_MAX_PIXELS = 64u * 1024u * 1024u;   // 64 MP safety bound

static u32 pngReadBE32(const u8 *p) {
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | (u32) p[3];
}

static u32 pngChannels(int colorType) {
    switch (colorType) {
        case 0: return 1;   // grey
        case 2: return 3;   // RGB
        case 4: return 2;   // grey + alpha
        case 6: return 4;   // RGBA
        default: return 0;  // palette / unsupported
    }
}

bool Png_isPng(const u8 *data, bytes length) {
    return data != nullptr && length >= sizeof(PNG_SIGNATURE)
        && memcmp(data, PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) == 0;
}

const char *Png_codeName(PngCode code) {
    switch (code) {
        case PNG_OK: return "ok";
        case PNG_INVALID: return "invalid";
        case PNG_UNSUPPORTED: return "unsupported";
        case PNG_TRUNCATED: return "truncated";
        case PNG_NO_MEMORY: return "no-memory";
        default: return "unknown";
    }
}

// Reconstruct the unfiltered scanlines in place. `raw` holds height rows of
// (1 filter byte + rowBytes). Filters: 0 none, 1 sub, 2 up, 3 average, 4 paeth.
static bool pngUnfilter(u8 *raw, u32 height, bytes rowBytes, u32 bpp) {
    bytes stride = rowBytes + 1;
    for (u32 y = 0; y < height; ++y) {
        u8 *row = raw + (bytes) y * stride;
        u8 filter = row[0];
        u8 *cur = row + 1;
        const u8 *prev = (y == 0) ? nullptr : raw + (bytes) (y - 1) * stride + 1;
        for (bytes x = 0; x < rowBytes; ++x) {
            u8 left = (x >= bpp) ? cur[x - bpp] : 0;
            u8 up = prev ? prev[x] : 0;
            u8 upLeft = (prev && x >= bpp) ? prev[x - bpp] : 0;
            switch (filter) {
                case 0: break;
                case 1: cur[x] = (u8) (cur[x] + left); break;
                case 2: cur[x] = (u8) (cur[x] + up); break;
                case 3: cur[x] = (u8) (cur[x] + (u8) (((u32) left + up) / 2u)); break;
                case 4: {
                    int p = (int) left + (int) up - (int) upLeft;
                    int pa = p > (int) left ? p - (int) left : (int) left - p;
                    int pb = p > (int) up ? p - (int) up : (int) up - p;
                    int pc = p > (int) upLeft ? p - (int) upLeft : (int) upLeft - p;
                    u8 pred = (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : upLeft);
                    cur[x] = (u8) (cur[x] + pred);
                    break;
                }
                default: return false;
            }
        }
    }
    return true;
}

static PngCode Png_decodeWith(const u8 *data, bytes length, u8 **outRgba,
                              u32 *outWidth, u32 *outHeight, bool useMalloc);

static any pngAlloc(bytes bytes, bool useMalloc) {
    return useMalloc ? malloc(bytes) : Memory_alloc(TYPE_BYTE_ARRAY, bytes);
}
static fn pngRelease(any block, bool useMalloc) {
    if (useMalloc) free(block);
    else Memory_free(block);
}

PngCode Png_decode(const u8 *data, bytes length, u8 **outRgba,
                   u32 *outWidth, u32 *outHeight) {
    return Png_decodeWith(data, length, outRgba, outWidth, outHeight, false);
}

PngCode Png_decodeMalloc(const u8 *data, bytes length, u8 **outRgba,
                         u32 *outWidth, u32 *outHeight) {
    return Png_decodeWith(data, length, outRgba, outWidth, outHeight, true);
}

static PngCode Png_decodeWith(const u8 *data, bytes length, u8 **outRgba,
                              u32 *outWidth, u32 *outHeight, bool useMalloc) {
    if (data == nullptr || outRgba == nullptr || outWidth == nullptr || outHeight == nullptr) {
        THROW("Png decode: null bytes or output");
        return PNG_INVALID;
    }
    if (!Png_isPng(data, length)) {
        THROW("Png decode: signature");
        return PNG_INVALID;
    }

    u32 width = 0, height = 0;
    u32 channels = 0;
    bytes idatTotal = 0;
    bool seenIHDR = false, seenIDAT = false, seenIEND = false;

    // Pass 1: validate every chunk (length, CRC, order) and size the IDAT stream.
    bytes at = sizeof(PNG_SIGNATURE);
    while (at + 12 <= length) {
        u32 chunkLen = pngReadBE32(data + at);
        const u8 *type = data + at + 4;
        if (chunkLen > PNG_MAX_PIXELS || at + 12u + chunkLen > length) {
            THROW("Png decode: chunk length");
            return PNG_TRUNCATED;
        }
        u32 expected = pngReadBE32(data + at + 8 + chunkLen);
        uLong computed = crc32(0L, Z_NULL, 0);
        computed = crc32(computed, type, 4);
        computed = crc32(computed, data + at + 8, chunkLen);
        if ((u32) computed != expected) {
            THROW("Png decode: chunk CRC");
            return PNG_INVALID;
        }
        if (memcmp(type, "IHDR", 4) == 0) {
            if (seenIHDR || chunkLen != 13)
                return PNG_INVALID;
            width = pngReadBE32(data + at + 8);
            height = pngReadBE32(data + at + 12);
            int bitDepth = data[at + 16];
            int colorType = data[at + 17];
            int interlace = data[at + 20];
            if (width == 0 || height == 0 || width > PNG_MAX_PIXELS || height > PNG_MAX_PIXELS
                || (u64) width * height > PNG_MAX_PIXELS) {
                THROW("Png decode: geometry");
                return PNG_INVALID;
            }
            if (bitDepth != 8 || pngChannels(colorType) == 0 || interlace != 0) {
                THROW("Png decode: unsupported IHDR (depth %d, color %d, interlace %d)",
                      bitDepth, colorType, interlace);
                return PNG_UNSUPPORTED;
            }
            channels = pngChannels(colorType);
            seenIHDR = true;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (!seenIHDR)
                return PNG_INVALID;
            idatTotal += chunkLen;
            seenIDAT = true;
        } else if (memcmp(type, "IEND", 4) == 0) {
            seenIEND = true;
            break;
        } else if ((type[0] & 0x20) == 0) {
            THROW("Png decode: unknown critical chunk");
            return PNG_UNSUPPORTED;   // unknown CRITICAL chunk: cannot ignore
        }
        at += 12u + chunkLen;
    }
    if (!seenIHDR || !seenIDAT || !seenIEND) {
        THROW("Png decode: missing IHDR/IDAT/IEND");
        return PNG_TRUNCATED;
    }

    bytes rowBytes = (bytes) width * channels;
    bytes rawSize = (bytes) height * (rowBytes + 1);
    if (rawSize == 0 || rawSize > PNG_MAX_PIXELS * 8u) {
        THROW("Png decode: raw size");
        return PNG_INVALID;
    }
    u8 *compressed = (u8*) pngAlloc(idatTotal ? idatTotal : 1, useMalloc);
    if (compressed == nullptr) {
        THROW("Png decode: idat allocation");
        return PNG_NO_MEMORY;
    }
    u8 *raw = (u8*) pngAlloc(rawSize, useMalloc);
    if (raw == nullptr) {
        pngRelease(compressed, useMalloc);
        THROW("Png decode: raw allocation");
        return PNG_NO_MEMORY;
    }

    // Pass 2: gather the IDAT payloads.
    bytes gathered = 0;
    at = sizeof(PNG_SIGNATURE);
    while (at + 12 <= length && gathered < idatTotal) {
        u32 chunkLen = pngReadBE32(data + at);
        const u8 *type = data + at + 4;
        if (memcmp(type, "IDAT", 4) == 0) {
            memcpy(compressed + gathered, data + at + 8, chunkLen);
            gathered += chunkLen;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12u + chunkLen;
    }

    uLongf rawLen = (uLongf) rawSize;
    int zc = uncompress(raw, &rawLen, compressed, (uLong) gathered);
    pngRelease(compressed, useMalloc);
    if (zc != Z_OK || rawLen != rawSize) {
        pngRelease(raw, useMalloc);
        THROW("Png decode: inflate failed (%d)", zc);
        return PNG_INVALID;
    }
    if (!pngUnfilter(raw, height, rowBytes, channels)) {
        pngRelease(raw, useMalloc);
        THROW("Png decode: filter type");
        return PNG_INVALID;
    }

    bytes outBytes = (bytes) width * height * 4u;
    u8 *rgba = (u8*) pngAlloc(outBytes, useMalloc);
    if (rgba == nullptr) {
        pngRelease(raw, useMalloc);
        THROW("Png decode: output allocation");
        return PNG_NO_MEMORY;
    }
    for (u32 y = 0; y < height; ++y) {
        const u8 *src = raw + (bytes) y * (rowBytes + 1) + 1;
        u8 *dst = rgba + (bytes) y * width * 4u;
        for (u32 x = 0; x < width; ++x) {
            switch (channels) {
                case 1: dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255; break;
                case 2: dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[1]; break;
                case 3: dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255; break;
                default: dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3]; break;
            }
            src += channels;
            dst += 4;
        }
    }
    pngRelease(raw, useMalloc);

    *outRgba = rgba;
    *outWidth = width;
    *outHeight = height;
    return PNG_OK;
}
