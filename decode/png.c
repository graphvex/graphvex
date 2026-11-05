#include "decode/png.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "exception/throw.h"
#include "annotation/annotation.h"

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

static const uint8_t PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
static const uint32_t PNG_MAX_PIXELS = 64u * 1024u * 1024u;   // 64 MP safety bound

static uint32_t pngReadBE32(const uint8_t *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

static uint32_t pngChannels(int colorType) {
    switch (colorType) {
        case 0: return 1;   // grey
        case 2: return 3;   // RGB
        case 4: return 2;   // grey + alpha
        case 6: return 4;   // RGBA
        default: return 0;  // palette / unsupported
    }
}

bool Png_isPng(const uint8_t *bytes, size_t length) {
    return bytes != nullptr && length >= sizeof(PNG_SIGNATURE)
        && memcmp(bytes, PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) == 0;
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
static bool pngUnfilter(uint8_t *raw, uint32_t height, size_t rowBytes, uint32_t bpp) {
    size_t stride = rowBytes + 1;
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t *row = raw + (size_t) y * stride;
        uint8_t filter = row[0];
        uint8_t *cur = row + 1;
        const uint8_t *prev = (y == 0) ? nullptr : raw + (size_t) (y - 1) * stride + 1;
        for (size_t x = 0; x < rowBytes; ++x) {
            uint8_t left = (x >= bpp) ? cur[x - bpp] : 0;
            uint8_t up = prev ? prev[x] : 0;
            uint8_t upLeft = (prev && x >= bpp) ? prev[x - bpp] : 0;
            switch (filter) {
                case 0: break;
                case 1: cur[x] = (uint8_t) (cur[x] + left); break;
                case 2: cur[x] = (uint8_t) (cur[x] + up); break;
                case 3: cur[x] = (uint8_t) (cur[x] + (uint8_t) (((uint32_t) left + up) / 2u)); break;
                case 4: {
                    int p = (int) left + (int) up - (int) upLeft;
                    int pa = p > (int) left ? p - (int) left : (int) left - p;
                    int pb = p > (int) up ? p - (int) up : (int) up - p;
                    int pc = p > (int) upLeft ? p - (int) upLeft : (int) upLeft - p;
                    uint8_t pred = (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : upLeft);
                    cur[x] = (uint8_t) (cur[x] + pred);
                    break;
                }
                default: return false;
            }
        }
    }
    return true;
}

static PngCode Png_decodeWith(const uint8_t *bytes, size_t length, uint8_t **outRgba,
                              uint32_t *outWidth, uint32_t *outHeight, bool useMalloc);

static void *pngAlloc(size_t bytes, bool useMalloc) {
    return useMalloc ? malloc(bytes) : Memory_alloc(TYPE_BYTE_ARRAY, bytes);
}
static void pngRelease(void *block, bool useMalloc) {
    if (useMalloc) free(block);
    else Memory_free(block);
}

PngCode Png_decode(const uint8_t *bytes, size_t length, uint8_t **outRgba,
                   uint32_t *outWidth, uint32_t *outHeight) {
    return Png_decodeWith(bytes, length, outRgba, outWidth, outHeight, false);
}

PngCode Png_decodeMalloc(const uint8_t *bytes, size_t length, uint8_t **outRgba,
                         uint32_t *outWidth, uint32_t *outHeight) {
    return Png_decodeWith(bytes, length, outRgba, outWidth, outHeight, true);
}

static PngCode Png_decodeWith(const uint8_t *bytes, size_t length, uint8_t **outRgba,
                              uint32_t *outWidth, uint32_t *outHeight, bool useMalloc) {
    if (bytes == nullptr || outRgba == nullptr || outWidth == nullptr || outHeight == nullptr) {
        THROW("Png decode: null bytes or output");
        return PNG_INVALID;
    }
    if (!Png_isPng(bytes, length)) {
        THROW("Png decode: signature");
        return PNG_INVALID;
    }

    uint32_t width = 0, height = 0;
    uint32_t channels = 0;
    size_t idatTotal = 0;
    bool seenIHDR = false, seenIDAT = false, seenIEND = false;

    // Pass 1: validate every chunk (length, CRC, order) and size the IDAT stream.
    size_t at = sizeof(PNG_SIGNATURE);
    while (at + 12 <= length) {
        uint32_t chunkLen = pngReadBE32(bytes + at);
        const uint8_t *type = bytes + at + 4;
        if (chunkLen > PNG_MAX_PIXELS || at + 12u + chunkLen > length) {
            THROW("Png decode: chunk length");
            return PNG_TRUNCATED;
        }
        uint32_t expected = pngReadBE32(bytes + at + 8 + chunkLen);
        uLong computed = crc32(0L, Z_NULL, 0);
        computed = crc32(computed, type, 4);
        computed = crc32(computed, bytes + at + 8, chunkLen);
        if ((uint32_t) computed != expected) {
            THROW("Png decode: chunk CRC");
            return PNG_INVALID;
        }
        if (memcmp(type, "IHDR", 4) == 0) {
            if (seenIHDR || chunkLen != 13)
                return PNG_INVALID;
            width = pngReadBE32(bytes + at + 8);
            height = pngReadBE32(bytes + at + 12);
            int bitDepth = bytes[at + 16];
            int colorType = bytes[at + 17];
            int interlace = bytes[at + 20];
            if (width == 0 || height == 0 || width > PNG_MAX_PIXELS || height > PNG_MAX_PIXELS
                || (uint64_t) width * height > PNG_MAX_PIXELS) {
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

    size_t rowBytes = (size_t) width * channels;
    size_t rawSize = (size_t) height * (rowBytes + 1);
    if (rawSize == 0 || rawSize > PNG_MAX_PIXELS * 8u) {
        THROW("Png decode: raw size");
        return PNG_INVALID;
    }
    uint8_t *compressed = (uint8_t*) pngAlloc(idatTotal ? idatTotal : 1, useMalloc);
    if (compressed == nullptr) {
        THROW("Png decode: idat allocation");
        return PNG_NO_MEMORY;
    }
    uint8_t *raw = (uint8_t*) pngAlloc(rawSize, useMalloc);
    if (raw == nullptr) {
        pngRelease(compressed, useMalloc);
        THROW("Png decode: raw allocation");
        return PNG_NO_MEMORY;
    }

    // Pass 2: gather the IDAT payloads.
    size_t gathered = 0;
    at = sizeof(PNG_SIGNATURE);
    while (at + 12 <= length && gathered < idatTotal) {
        uint32_t chunkLen = pngReadBE32(bytes + at);
        const uint8_t *type = bytes + at + 4;
        if (memcmp(type, "IDAT", 4) == 0) {
            memcpy(compressed + gathered, bytes + at + 8, chunkLen);
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

    size_t outBytes = (size_t) width * height * 4u;
    uint8_t *rgba = (uint8_t*) pngAlloc(outBytes, useMalloc);
    if (rgba == nullptr) {
        pngRelease(raw, useMalloc);
        THROW("Png decode: output allocation");
        return PNG_NO_MEMORY;
    }
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *src = raw + (size_t) y * (rowBytes + 1) + 1;
        uint8_t *dst = rgba + (size_t) y * width * 4u;
        for (uint32_t x = 0; x < width; ++x) {
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
