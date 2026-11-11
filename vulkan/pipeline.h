#ifndef GRAPHICS_PIPELINE_H
#define GRAPHICS_PIPELINE_H

#include <stdbool.h>
#include <stdint.h>

// graphvex R3 — vulkan/pipeline.h
//
// ONE pipeline draws everything: rounded rects, images, glyph masks. It owns
// the vertex/fragment SPIR-V (compiled by `b.sh` from vulkan/shaders/*.vert|frag)
// and the fixed state. No swapchain, no render pass tied to a window — the
// pipeline renders into whatever Image/Board is the current target.

typedef struct Pipeline Pipeline;

typedef struct PipelineDesc {
    const void *vertSpirv;    // SPIR-V words
    u32 vertSize;        // Bytes
    const void *fragSpirv;
    u32 fragSize;
    u32 vertexStride;    // Bytes per vertex (VK_VERTEX_FLOATS * 4)
    u32 pushConstantSize;// e.g. sizeof(vec2 viewport)
} PipelineDesc;

Pipeline *Pipeline_new(const PipelineDesc *desc);

// Convenience: load the two .spv files produced by `b.sh` (glslangValidator).
Pipeline *Pipeline_fromFiles(const char *vertSpvPath, const char *fragSpvPath,
                             u32 vertexStride, u32 pushConstantSize);

void Pipeline_destroy(Pipeline *pipeline);
bool Pipeline_isValid(const Pipeline *pipeline);
u32 Pipeline_vertexStride(const Pipeline *pipeline);
void *Pipeline_native(const Pipeline *pipeline);   // opaque VkPipeline (once bound)

#endif // GRAPHICS_PIPELINE_H
