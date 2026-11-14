#ifndef GRAPHVEX_DECODE_PNG_H
#define GRAPHVEX_DECODE_PNG_H

#include "vex.h"

// decode/png.h — decode a PNG byte stream into RGBA8.
//
// MODULE: Png (no owned class, no persistent state)
//
// This is a PURE algorithm: bytes in, pixels out. It reads no file, opens no
// path and holds no handle — the caller obtains the bytes (Relational Engine's
// File owns that) and hands them here, and the returned buffer is allocated
// through the engine's Memory ABI so the caller holds a stable storage pointer
// to free with Memory_free.
//
// Supported: PNG signature, IHDR, IDAT (concatenated), IEND; bit depth 8;
// colour types 0 (grey), 2 (RGB), 4 (grey+alpha), 6 (RGBA); filter types 0-4.
// Unsupported and rejected plainly: interlacing, 16-bit, palette (type 3) and
// sub-8-bit depths, plus any non-IHDR/IDAT/IEND critical chunk.
//
// Adversarial input (truncated, malformed chunk, bad CRC, wrong length,
// out-of-range dimensions, decompression that does not match the declared
// geometry) is rejected with a code and leaves the outputs untouched.

typedef enum PngCode {
    PNG_OK = 0,
    PNG_INVALID,       // bad signature, malformed chunk, CRC mismatch, bad data
    PNG_UNSUPPORTED,   // interlace, 16-bit, palette, or sub-8-bit depth
    PNG_TRUNCATED,     // the stream ended early
    PNG_NO_MEMORY      // the engine allocator refused
} PngCode;

// True when the bytes begin with the 8-byte PNG signature.
bool Png_isPng(const u8 *data, bytes length);

// Decode to RGBA8. On PNG_OK: *outRgba is a Memory_alloc-backed buffer of
// width*height*4 bytes (release with Memory_free), *outWidth/*outHeight set.
// On any failure the outputs are untouched and no allocation is retained.
PngCode Png_decode(const u8 *data, bytes length, u8 **outRgba,
                   u32 *outWidth, u32 *outHeight);

// Same decode, but the RGBA buffer comes from libc malloc so a caller that is not
// the engine (a lean host without an arena) can decode its own asset. Release it
// with free(). Everything else is identical.
PngCode Png_decodeMalloc(const u8 *data, bytes length, u8 **outRgba,
                         u32 *outWidth, u32 *outHeight);

// Stable name for a code (diagnostics/projections).
const char *Png_codeName(PngCode code);

#endif
