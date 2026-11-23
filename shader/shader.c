/** shader/shader.c -- multi-source shader registry and re/io/file byte loader. */

#include "vex.h"
#include "shader/shader.h"
#include "nio/graphvex_memory.h"
#include "re/io/file/file.h"

#if defined(VEX_PLATFORM_APPLE)
#include <mach-o/dyld.h>
#include <stdio.h>
#include <string.h>
#endif

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Shader
 * ============================================================================
 * Multi-source shader metadata and file byte loader for graphvex/api. Resolves
 * preferred shading languages (MSL, GLSL, WGSL, SPIR-V) per native target and
 * loads shader source or bytecode blobs into Graphvex_arena() via re/io/file.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ShaderSource, ShaderBytes (shader/shader.c)
 * ============================================================================
 * ShaderSource describes available language variants for a shader module;
 * ShaderBytes owns a NUL-terminated buffer loaded from disk or app bundle.
 *
 * --FIELDS-------------------------------
 * public   string moduleName             borrows module name, 1:1
 * public   ShaderStage   stage                  shader pipeline stage
 * public   u32           availableLanguageMask  bitmask of ShaderLanguage
 * public   u8           *data                   owns loaded bytes, 1:N
 * public   bytes         length                 byte length excluding NUL
 *
 * --FUNCTIONS----------------------------
 * public   ShaderSource_supportsLanguage(...)   checks backend compatibility
 * public   ShaderSource_preferredLanguage(...)  selects best language variant
 * public   ShaderBytes_isValid(...)             checks non-null non-empty blob
 * public   Shader_loadFile(...)                 loads shader file into arena
 * public   ShaderBytes_destroy(...)             frees loaded byte buffer
 *
 * --GETTERS------------------------------
 * public   ShaderSource_moduleName/stage(...)   shader metadata accessors
 * public   ShaderBytes_data/length(...)         loaded buffer accessors
 *
 * --SETTERS------------------------------
 * public   ShaderSource_setModuleName/Stage     shader metadata mutators
 * private  ShaderBytes_setData/setLength(...)   internal buffer mutators
 * ============================================================================
 */

;;GETTER
string ShaderSource_moduleName(const ShaderSource *self) {
    return self ? (*self).moduleName : nullptr;
}

;;SETTER
fn ShaderSource_setModuleName(ShaderSource *self, string name) {
    if (self)
        (*self).moduleName = name;
}

;;GETTER
ShaderStage ShaderSource_stage(const ShaderSource *self) {
    return self ? (*self).stage : SHADER_STAGE_VERTEX;
}

;;SETTER
fn ShaderSource_setStage(ShaderSource *self, ShaderStage stage) {
    if (self)
        (*self).stage = stage;
}

;;GETTER
u32 ShaderSource_availableLanguageMask(const ShaderSource *self) {
    return self ? (*self).availableLanguageMask : 0u;
}

;;SETTER
fn ShaderSource_setAvailableLanguageMask(ShaderSource *self, u32 mask) {
    if (self)
        (*self).availableLanguageMask = mask;
}

/** MSL-only modules can only execute on an MSL-native target (no MSL  to  SPIR-V). */
bool ShaderSource_supportsLanguage(const ShaderSource *self, ShaderLanguage nativeLang) {
    if (!self)
        return false;
    u32 mask = ShaderSource_availableLanguageMask(self);
    bool hasGlsl  = (mask & (1u << SHADER_LANG_GLSL))  != 0u;
    bool hasMsl   = (mask & (1u << SHADER_LANG_MSL))   != 0u;
    bool hasWgsl  = (mask & (1u << SHADER_LANG_WGSL))  != 0u;
    bool hasSpirv = (mask & (1u << SHADER_LANG_SPIRV)) != 0u;

    if (nativeLang == SHADER_LANG_MSL)
        return hasMsl || hasGlsl || hasWgsl || hasSpirv;
    return hasGlsl || hasWgsl || hasSpirv;
}

ShaderLanguage ShaderSource_preferredLanguage(const ShaderSource *self, ShaderLanguage nativeLang) {
    if (!self)
        return SHADER_LANG_SPIRV;
    u32 mask = ShaderSource_availableLanguageMask(self);
    if (nativeLang < SHADER_LANG_COUNT && (mask & (1u << nativeLang)))
        return nativeLang;
    if (mask & (1u << SHADER_LANG_GLSL))
        return SHADER_LANG_GLSL;
    if (mask & (1u << SHADER_LANG_WGSL))
        return SHADER_LANG_WGSL;
    return SHADER_LANG_SPIRV;
}

;;GETTER
u8 *ShaderBytes_data(const ShaderBytes *self) {
    return self ? (*self).data : nullptr;
}

;;SETTER
static fn ShaderBytes_setData(ShaderBytes *self, u8 *data) {
    if (self)
        (*self).data = data;
}

;;GETTER
bytes ShaderBytes_length(const ShaderBytes *self) {
    return self ? (*self).length : 0u;
}

;;SETTER
static fn ShaderBytes_setLength(ShaderBytes *self, bytes length) {
    if (self)
        (*self).length = length;
}

bool ShaderBytes_isValid(const ShaderBytes *self) {
    return ShaderBytes_data(self) != nullptr && ShaderBytes_length(self) > 0u;
}

static u8 *ShaderBytes_initBuffer(bytes len) {
    return (u8*) Memory_alloc(ID_SHADER_BYTES, Graphvex_arena(), len + 1u);
}

#if defined(VEX_PLATFORM_APPLE)
static bool shaderResolveBundlePath(string relPath, utf8 *outBuf, bytes outCap) {
    utf8 exePath[1024];
    u32 exeLen = (u32) sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &exeLen) != 0)
        return false;
    utf8 *slash = strrchr(exePath, '/');
    if (!slash)
        return false;
    *slash = '\0';
    i32 n = snprintf(outBuf, outCap, "%s/../Resources/%s", exePath, relPath);
    return n > 0 && (bytes) n < outCap && File_exists(outBuf);
}
#endif

/**
 * Loads a shader source or bytecode file via re/io/file into Graphvex_arena()
 * with a trailing NUL byte so both text sources (.metal/.glsl) and binary blobs
 * (.metallib/.spv) work cleanly. Falls back to macOS .app Contents/Resources/.
 */
ShaderBytes Shader_loadFile(string path) {
    ShaderBytes out = { 0 };
    if (!path || path[0] == '\0')
        return out;

    string resolved = path;
#if defined(VEX_PLATFORM_APPLE)
    utf8 bundleBuf[1536];
    if (!File_exists(resolved)) {
        if (!shaderResolveBundlePath(path, bundleBuf, sizeof(bundleBuf)))
            return out;
        resolved = bundleBuf;
    }
#else
    if (!File_exists(resolved))
        return out;
#endif

    File *f = File_open(resolved, FILE_MODE_READ);
    if (!f)
        return out;
    i64 sz = File_size(f);
    if (sz <= 0) {
        File_close(f);
        return out;
    }

    bytes len = (bytes) sz;
    u8 *buf = ShaderBytes_initBuffer(len);
    if (!buf) {
        File_close(f);
        return out;
    }
    i64 got = File_read(f, buf, sz);
    File_close(f);
    if (got != sz) {
        ShaderBytes_setData(&out, buf);
        ShaderBytes_destroy(&out);
        return out;
    }
    buf[len] = 0u;
    ShaderBytes_setData(&out, buf);
    ShaderBytes_setLength(&out, len);
    return out;
}

;;DECONSTRUCT
fn ShaderBytes_destroy(ShaderBytes *self) {
    if (!self)
        return;
    u8 *buf = ShaderBytes_data(self);
    if (buf)
        Memory_free(buf);
    ShaderBytes_setData(self, nullptr);
    ShaderBytes_setLength(self, 0u);
}
