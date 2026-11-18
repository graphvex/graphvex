// shader/shader.c — multi-source shader registry (GLSL | MSL | WGSL -> SPIR-V).

#include "vex.h"
#include "shader/shader.h"

;;OVERVIEW
;;DEFINITION

;;GETTER
const char *ShaderSource_moduleName(const ShaderSource *self) {
    return self ? (*self).moduleName : nullptr;
}
;;SETTER
fn ShaderSource_setModuleName(ShaderSource *self, const char *name) {
    if (self) (*self).moduleName = name;
}

;;GETTER
ShaderStage ShaderSource_stage(const ShaderSource *self) {
    return self ? (*self).stage : SHADER_STAGE_VERTEX;
}
;;SETTER
fn ShaderSource_setStage(ShaderSource *self, ShaderStage stage) {
    if (self) (*self).stage = stage;
}

;;GETTER
u32 ShaderSource_availableLanguageMask(const ShaderSource *self) {
    return self ? (*self).availableLanguageMask : 0u;
}
;;SETTER
fn ShaderSource_setAvailableLanguageMask(ShaderSource *self, u32 mask) {
    if (self) (*self).availableLanguageMask = mask;
}

bool ShaderSource_supportsBackend(const ShaderSource *self, BackendKind backend) {
    if (!self) return false;
    u32 mask = ShaderSource_availableLanguageMask(self);
    u32 hasGlsl  = (mask & (1u << SHADER_LANG_GLSL))  != 0u;
    u32 hasMsl   = (mask & (1u << SHADER_LANG_MSL))   != 0u;
    u32 hasWgsl  = (mask & (1u << SHADER_LANG_WGSL))  != 0u;
    u32 hasSpirv = (mask & (1u << SHADER_LANG_SPIRV)) != 0u;

    if (backend == BACKEND_METAL) {
        return hasMsl || hasGlsl || hasWgsl || hasSpirv;
    }
    // Non-Metal backends cannot consume an MSL-only module (no MSL -> SPIR-V path).
    return hasGlsl || hasWgsl || hasSpirv;
}

ShaderLanguage ShaderSource_preferredLanguage(const ShaderSource *self, BackendKind backend) {
    if (!self) return SHADER_LANG_SPIRV;
    u32 mask = ShaderSource_availableLanguageMask(self);
    if (backend == BACKEND_METAL && (mask & (1u << SHADER_LANG_MSL))) {
        return SHADER_LANG_MSL;
    }
    if (backend == BACKEND_WGPU && (mask & (1u << SHADER_LANG_WGSL))) {
        return SHADER_LANG_WGSL;
    }
    if (mask & (1u << SHADER_LANG_GLSL)) {
        return SHADER_LANG_GLSL;
    }
    if (mask & (1u << SHADER_LANG_WGSL)) {
        return SHADER_LANG_WGSL;
    }
    return SHADER_LANG_SPIRV;
}
