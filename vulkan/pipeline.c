#include "vex.h"
#include "vulkan/pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/** Pipeline owns copied vertex and fragment SPIR-V bytes plus the immutable
 * vertex/push-constant geometry needed by a renderer. The current unit does not
 * create or bind a native VkPipeline; native is an opaque reserved projection. */
;;OVERVIEW
/** ========================================================================
 * CLASS: Pipeline (vulkan/pipeline.c)
 * STRUCT FIELDS (Mirroring Pipeline)
 * any vert                     owned vertex SPIR-V byte copy
 * u32 vertSize              vertex byte length
 * any frag                     owned fragment SPIR-V byte copy
 * u32 fragSize              fragment byte length
 * u32 vertexStride          configured bytes per vertex
 * u32 pushConstantSize      configured push-constant byte extent
 * any native                   reserved opaque native pipeline handle
 * FUNCTION REGISTRY
 * Constructors: Pipeline *Pipeline_new(const PipelineDesc *desc)
 *   Pipeline *Pipeline_fromFiles(const char *vertSpvPath,
 *     const char *fragSpvPath, u32 vertexStride, u32 pushConstantSize)
 * Lifecycle: fn Pipeline_destroy(Pipeline *pipeline)
 * Projections: bool Pipeline_isValid(const Pipeline *pipeline)
 *   u32 Pipeline_vertexStride(const Pipeline *pipeline)
 *   any Pipeline_native(const Pipeline *pipeline)
 * Private helper: slurp reads one nonempty shader file into owned bytes.
 * Admission, failure and ownership contract:
 * Constructors copy supplied/file bytes; temporary file buffers are freed after
 * construction. Rejection returns nullptr and preserves no partial Pipeline.
 * Destroy accepts nullptr. Native pipeline creation is not implemented here.
 * ======================================================================== */

struct Pipeline {
    any vert;         // SPIR-V Bytes
    u32 vertSize;
    any frag;
    u32 fragSize;
    u32 vertexStride;
    u32 pushConstantSize;
    any native;       // opaque VkPipeline
};

;;GETTER
static inline any Pipeline_vert(const Pipeline *self) { return (*self).vert; }
;;SETTER
static inline fn Pipeline_setVert(Pipeline *self, any v) { (*self).vert = v; }
;;SETTER
static inline fn Pipeline_setVertSize(Pipeline *self, u32 v) { (*self).vertSize = v; }
;;GETTER
static inline any Pipeline_frag(const Pipeline *self) { return (*self).frag; }
;;SETTER
static inline fn Pipeline_setFrag(Pipeline *self, any v) { (*self).frag = v; }
;;SETTER
static inline fn Pipeline_setFragSize(Pipeline *self, u32 v) { (*self).fragSize = v; }
;;SETTER
static inline fn Pipeline_setVertexStride(Pipeline *self, u32 v) { (*self).vertexStride = v; }
;;SETTER
static inline fn Pipeline_setPushConstantSize(Pipeline *self, u32 v) { (*self).pushConstantSize = v; }

static any slurp(const char *path, u32 *outSize) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    i64 n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return nullptr; }
    any buf = malloc((bytes)n);
    if (buf && fread(buf, 1, (bytes)n, f) != (bytes)n) {
        free(buf);
        buf = nullptr;
    }
    fclose(f);
    if (buf) *outSize = (u32)n;
    return buf;
}

Pipeline *Pipeline_new(const PipelineDesc *desc) {
    if (!desc) return nullptr;
    PipelineDesc d = *desc;
    if (!d.vertSpirv || !d.fragSpirv) return nullptr;
    Pipeline *p = calloc(1, sizeof *p);
    if (!p) return nullptr;
    any v = malloc(d.vertSize);
    any f = malloc(d.fragSize);
    Pipeline_setVert(p, v);
    Pipeline_setFrag(p, f);
    if (!v || !f) {
        Pipeline_destroy(p);
        return nullptr;
    }
    memcpy(v, d.vertSpirv, d.vertSize);
    memcpy(f, d.fragSpirv, d.fragSize);
    Pipeline_setVertSize(p, d.vertSize);
    Pipeline_setFragSize(p, d.fragSize);
    Pipeline_setVertexStride(p, d.vertexStride);
    Pipeline_setPushConstantSize(p, d.pushConstantSize);
    return p;
}

Pipeline *Pipeline_fromFiles(const char *vertSpvPath, const char *fragSpvPath,
                             u32 vertexStride, u32 pushConstantSize) {
    u32 vs = 0, fs = 0;
    any v = slurp(vertSpvPath, &vs);
    any f = slurp(fragSpvPath, &fs);
    if (!v || !f) {
        free(v);
        free(f);
        return nullptr;
    }
    PipelineDesc d = {v, vs, f, fs, vertexStride, pushConstantSize};
    Pipeline *p = Pipeline_new(&d);
    free(v);
    free(f);
    return p;
}

// Destroys the owned pipeline and layout objects; null is ignored.
fn Pipeline_destroy(Pipeline *pipeline) {
    if (!pipeline) return;
    free(Pipeline_vert(pipeline));
    free(Pipeline_frag(pipeline));
    free(pipeline);
}

// Reports whether the wrapper has vertex and fragment stages and a positive vertex stride.
bool Pipeline_isValid(const Pipeline *pipeline) {
    return pipeline && Pipeline_vert(pipeline) && Pipeline_frag(pipeline) && Pipeline_vertexStride(pipeline) > 0;
}

// Returns the configured vertex stride, or zero when pipeline is null.
u32 Pipeline_vertexStride(const Pipeline *pipeline) {
    return pipeline ? (*pipeline).vertexStride : 0u;
}

any Pipeline_native(const Pipeline *pipeline) { return pipeline ? (*pipeline).native : nullptr; }
