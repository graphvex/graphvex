// Owner: Png — pure PNG bytes → RGBA8. Real streams built in memory via zlib,
// plus the adversarial matrix (signature, CRC, truncation, unsupported, null).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "decode/png.h"
#include "nio/mem.h"
#include "test_support.h"

static bytes putChunk(u8 *out, const char *type, const u8 *data, bytes len) {
    out[0] = (u8) (len >> 24); out[1] = (u8) (len >> 16);
    out[2] = (u8) (len >> 8);  out[3] = (u8) len;
    memcpy(out + 4, type, 4);
    if (len)
        memcpy(out + 8, data, len);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*) type, 4);
    if (len)
        crc = crc32(crc, data, len);
    out[8 + len] = (u8) (crc >> 24); out[9 + len] = (u8) (crc >> 16);
    out[10 + len] = (u8) (crc >> 8); out[11 + len] = (u8) crc;
    return 12 + len;
}

// Build a PNG: raw scanlines (already filter-prefixed) compressed into one IDAT.
static bytes buildPng(u8 *out, u32 width, u32 height, u8 colorType,
                       const u8 *raw, bytes rawLen) {
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    memcpy(out, sig, 8);
    bytes at = 8;
    u8 ihdr[13];
    ihdr[0] = (u8) (width >> 24); ihdr[1] = (u8) (width >> 16);
    ihdr[2] = (u8) (width >> 8);  ihdr[3] = (u8) width;
    ihdr[4] = (u8) (height >> 24); ihdr[5] = (u8) (height >> 16);
    ihdr[6] = (u8) (height >> 8);  ihdr[7] = (u8) height;
    ihdr[8] = 8;            // bit depth
    ihdr[9] = colorType;
    ihdr[10] = 0; ihdr[11] = 0;   // deflate / adaptive filtering
    ihdr[12] = 0;                 // no interlace
    at += putChunk(out + at, "IHDR", ihdr, 13);
    uLongf clen = compressBound((uLong) rawLen);
    u8 *comp = (u8*) malloc(clen);
    if (compress2(comp, &clen, raw, (uLong) rawLen, 9) != Z_OK)
        return 0;
    at += putChunk(out + at, "IDAT", comp, (bytes) clen);
    free(comp);
    at += putChunk(out + at, "IEND", nullptr, 0);
    return at;
}

#define REJECT(code, expression, fragment) do { \
    beginDiagnostics(); \
    assert((expression) == (code)); \
    endDiagnostics(1, fragment); \
} while (0)

// Owner-local stderr capture: each rejection must emit exactly one diagnosed
// line naming its cause, and admitted paths must stay quiet.
static FILE *diagnostics;
static int savedStderr;
static fn beginDiagnostics() {
    fflush(stderr);
    diagnostics = tmpfile();
    assert(diagnostics != nullptr);
    savedStderr = dup(STDERR_FILENO);
    assert(savedStderr >= 0);
    assert(dup2(fileno(diagnostics), STDERR_FILENO) >= 0);
}
static fn endDiagnostics(bytes expected, const char *fragment) {
    fflush(stderr);
    assert(dup2(savedStderr, STDERR_FILENO) >= 0);
    close(savedStderr);
    rewind(diagnostics);
    char text[4096];
    bytes n = fread(text, 1, sizeof(text) - 1, diagnostics);
    text[n] = 0;
    bytes lines = 0;
    for (bytes i = 0; i < n; ++i)
        if (text[i] == '\n')
            ++lines;
    assert(lines == expected);
    if (expected != 0)
        assert(strstr(text, "[vex] ") != nullptr);
    if (fragment != nullptr)
        assert(strstr(text, fragment) != nullptr);
    fclose(diagnostics);
}

int main() {
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    assert(Png_isPng(sig, sizeof sig));
    assert(!Png_isPng(nullptr, 8) && !Png_isPng(sig, 4));
    assert(strcmp(Png_codeName(PNG_OK), "ok") == 0 && strcmp(Png_codeName(PNG_UNSUPPORTED), "unsupported") == 0);
    assert(strcmp(Png_codeName(PNG_TRUNCATED), "truncated") == 0
        && strcmp(Png_codeName(PNG_NO_MEMORY), "no-memory") == 0
        && strcmp(Png_codeName(PNG_INVALID), "invalid") == 0
        && strcmp(Png_codeName((PngCode) 99), "unknown") == 0);

    u8 png[512];
    u8 *rgba = nullptr;
    u32 w = 0, h = 0;

    // 2x2 RGBA, filter 0.
    {
        const u8 raw[] = {
            0, 10, 20, 30, 200,  40, 50, 60, 210,
            0, 70, 80, 90, 220, 100, 110, 120, 230,
        };
        bytes n = buildPng(png, 2, 2, 6, raw, sizeof raw);
        assert(n > 0);
        assert(Png_decode(png, n, &rgba, &w, &h) == PNG_OK);
        assert(w == 2 && h == 2);
        assert(rgba[0] == 10 && rgba[1] == 20 && rgba[2] == 30 && rgba[3] == 200);
        assert(rgba[12] == 100 && rgba[13] == 110 && rgba[14] == 120 && rgba[15] == 230);
        Memory_free(rgba);
    }

    // The malloc-backed entry point (a host with no engine arena) decodes the same
    // stream and is released with free().
    {
        const u8 raw[] = { 0, 9, 8, 7 };
        bytes n = buildPng(png, 1, 1, 2, raw, sizeof raw);
        u8 *heap = nullptr;
        u32 hw = 0, hh = 0;
        assert(Png_decodeMalloc(png, n, &heap, &hw, &hh) == PNG_OK);
        assert(hw == 1 && hh == 1 && heap[0] == 9 && heap[1] == 8 && heap[2] == 7 && heap[3] == 255);
        free(heap);
        assert(Png_decodeMalloc(png, n, nullptr, &hw, &hh) == PNG_INVALID);
    }

    // 2x1 RGB, filter 0 → alpha forced opaque.
    {
        const u8 raw[] = { 0, 1, 2, 3, 4, 5, 6 };
        bytes n = buildPng(png, 2, 1, 2, raw, sizeof raw);
        assert(Png_decode(png, n, &rgba, &w, &h) == PNG_OK);
        assert(w == 2 && h == 1 && rgba[0] == 1 && rgba[2] == 3 && rgba[3] == 255);
        assert(rgba[4] == 4 && rgba[7] == 255);
        Memory_free(rgba);
    }

    // 2x1 grey, filter 1 (Sub) on row 0 exercises reconstruction.
    {
        const u8 raw[] = { 1, 50, 10 };   // sub: 50, 50+10
        bytes n = buildPng(png, 2, 1, 0, raw, sizeof raw);
        assert(Png_decode(png, n, &rgba, &w, &h) == PNG_OK);
        assert(rgba[0] == 50 && rgba[4] == 60 && rgba[3] == 255);
        Memory_free(rgba);
    }

    // Adversarial matrix.
    {
        const u8 raw[] = { 0, 1, 2, 3, 4, 5, 6 };
        bytes n = buildPng(png, 2, 1, 2, raw, sizeof raw);

        u8 bad[512];
        memcpy(bad, png, n);
        bad[0] = 0x00;                                   // broken signature
        REJECT(PNG_INVALID, Png_decode(bad, n, &rgba, &w, &h), "signature");

        memcpy(bad, png, n);
        bad[20] ^= 0xFF;                                 // corrupt IHDR payload → CRC
        REJECT(PNG_INVALID, Png_decode(bad, n, &rgba, &w, &h), "CRC");

        REJECT(PNG_TRUNCATED, Png_decode(png, n - 4, &rgba, &w, &h), "missing");

        u8 palette[64];
        bytes pn = buildPng(palette, 1, 1, 3, (const u8[]) { 0, 0 }, 2);
        REJECT(PNG_UNSUPPORTED, Png_decode(palette, pn, &rgba, &w, &h), "unsupported IHDR");

        REJECT(PNG_INVALID, Png_decode(png, n, nullptr, &w, &h), "null bytes or output");
        REJECT(PNG_INVALID, Png_decode(png, n, &rgba, nullptr, &h), "null bytes or output");

        // A stream whose IDAT inflates to the wrong size is rejected, not padded.
        u8 shortRaw[64];
        bytes sn = buildPng(shortRaw, 4, 4, 2, (const u8[]) { 0, 1, 2, 3 }, 4);
        REJECT(PNG_INVALID, Png_decode(shortRaw, sn, &rgba, &w, &h), "inflate failed");
    }

    printf("png_test: PASS\n");
    return 0;
}
