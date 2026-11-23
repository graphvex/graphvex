#ifndef GRAPHVEX_SHADER_H
#define GRAPHVEX_SHADER_H

/**
 * shader/shader.h -- multi-source shader registry (GLSL | MSL | WGSL  to  SPIR-V)
 * and re/io/file byte-stream loader for .metal, .metallib, and .spv blobs.
 */

#include "core/core.h"

#define ID_SHADER_BYTES 0x0105u

;;CLASSNAME("ShaderLanguage")

typedef enum ShaderLanguage {
    SHADER_LANG_GLSL  = 0,
    SHADER_LANG_MSL   = 1,
    SHADER_LANG_WGSL  = 2,
    SHADER_LANG_SPIRV = 3,
    SHADER_LANG_DXIL  = 4,
    SHADER_LANG_COUNT = 5
} ShaderLanguage;

;;CLASSNAME("ShaderStage")

typedef enum ShaderStage {
    SHADER_STAGE_VERTEX   = 0,
    SHADER_STAGE_FRAGMENT = 1,
    SHADER_STAGE_COMPUTE  = 2,
    SHADER_STAGE_MESH     = 3
} ShaderStage;

;;CLASSNAME("ShaderSource")

typedef struct ShaderSource {
    string moduleName;
    ShaderStage stage;
    u32 availableLanguageMask;
    const u8 *spirvBytes;
    bytes spirvLength;
} ShaderSource;

_Static_assert(sizeof(ShaderSource) == 32, "ShaderSource obeys 32-byte floor");

;;GETTER
string ShaderSource_moduleName(const ShaderSource *self);
;;SETTER
fn ShaderSource_setModuleName(ShaderSource *self, string name);

;;GETTER
ShaderStage ShaderSource_stage(const ShaderSource *self);
;;SETTER
fn ShaderSource_setStage(ShaderSource *self, ShaderStage stage);

;;GETTER
u32 ShaderSource_availableLanguageMask(const ShaderSource *self);
;;SETTER
fn ShaderSource_setAvailableLanguageMask(ShaderSource *self, u32 mask);

bool ShaderSource_supportsLanguage(const ShaderSource *self, ShaderLanguage nativeLang);
ShaderLanguage ShaderSource_preferredLanguage(const ShaderSource *self, ShaderLanguage nativeLang);

;;CLASSNAME("ShaderBytes")

typedef struct ShaderBytes {
    u8 *data;
    bytes length;
    u64 _pad[2];
} ShaderBytes;

_Static_assert(sizeof(ShaderBytes) == 32, "ShaderBytes obeys 32-byte floor");

;;GETTER
u8 *ShaderBytes_data(const ShaderBytes *self);
;;GETTER
bytes ShaderBytes_length(const ShaderBytes *self);

bool ShaderBytes_isValid(const ShaderBytes *self);
ShaderBytes Shader_loadFile(string path);
fn ShaderBytes_destroy(ShaderBytes *self);

#endif
