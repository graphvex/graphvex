#ifndef GRAPHVEX_SHADER_H
#define GRAPHVEX_SHADER_H

// shader/shader.h — multi-source shader registry (GLSL | MSL | WGSL -> SPIR-V).
//
// SPIR-V Shader Deployment Law: shader sources are GLSL, MSL or WGSL; SPIR-V is
// the common target. Each backend picks its native source when present and
// cross-compiles otherwise. MSL-only modules are Metal-only by design.

#include "core/core.h"

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
    const char *moduleName;
    ShaderStage stage;
    u32 availableLanguageMask;
    const u8 *spirvBytes;
    bytes spirvLength;
} ShaderSource;

_Static_assert(sizeof(ShaderSource) == 32, "ShaderSource obeys 32-byte floor");

;;GETTER
const char *ShaderSource_moduleName(const ShaderSource *self);
;;SETTER
fn ShaderSource_setModuleName(ShaderSource *self, const char *name);

;;GETTER
ShaderStage ShaderSource_stage(const ShaderSource *self);
;;SETTER
fn ShaderSource_setStage(ShaderSource *self, ShaderStage stage);

;;GETTER
u32 ShaderSource_availableLanguageMask(const ShaderSource *self);
;;SETTER
fn ShaderSource_setAvailableLanguageMask(ShaderSource *self, u32 mask);

bool ShaderSource_supportsBackend(const ShaderSource *self, BackendKind backend);
ShaderLanguage ShaderSource_preferredLanguage(const ShaderSource *self, BackendKind backend);

#endif
