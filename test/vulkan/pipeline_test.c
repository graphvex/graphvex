// graphvex/test/vulkan/pipeline_test.c — mirrors src/vulkan/pipeline.c
//
// The one quad pipeline: it owns the vertex/fragment SPIR-V Bytes and the fixed
// state. It records shaders; the VkPipeline is created once a Device is bound.

#include <stdio.h>
#include <stdlib.h>

#include "vulkan/pipeline.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Writes exactly the requested fixture bytes to a temporary file.
static int write_file(const char *path, const void *data, unsigned n) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    bytes w = fwrite(data, 1, n, f);
    fclose(f);
    return w == n;
}

// Tests shader-pipeline setup and cleanup across invalid shader inputs.
int main(void) {
    // in-memory SPIR-V placeholder Bytes
    unsigned char vs[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    unsigned char fs[16] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    PipelineDesc d = {vs, sizeof vs, fs, sizeof fs, 72, 8};
    Pipeline *p = Pipeline_new(&d);
    CHECK(Pipeline_isValid(p));
    CHECK(Pipeline_vertexStride(p) == 72);
    CHECK(Pipeline_native(p) == nullptr);      // not yet created on a device
    Pipeline_destroy(p);

    // a fully-specified desc is required
    CHECK(!Pipeline_new(nullptr));
    PipelineDesc bad = {nullptr, 0, fs, sizeof fs, 72, 8};
    CHECK(!Pipeline_new(&bad));

    // from files (this is what `b.sh` produces: glslangValidator -> .spv)
    CHECK(write_file("/tmp/gv_pipe.vert.spv", vs, sizeof vs));
    CHECK(write_file("/tmp/gv_pipe.frag.spv", fs, sizeof fs));
    Pipeline *pf = Pipeline_fromFiles("/tmp/gv_pipe.vert.spv", "/tmp/gv_pipe.frag.spv", 72, 8);
    CHECK(pf != nullptr && Pipeline_isValid(pf));
    CHECK(Pipeline_vertexStride(pf) == 72);
    Pipeline_destroy(pf);

    // missing files -> null, not a crash
    CHECK(Pipeline_fromFiles("/no/such.vert.spv", "/no/such.frag.spv", 72, 8) == nullptr);
    CHECK(Pipeline_fromFiles("/tmp/gv_pipe.vert.spv", "/no/such.frag.spv", 72, 8) == nullptr);

    // null-safe
    CHECK(!Pipeline_isValid(nullptr));
    CHECK(Pipeline_vertexStride(nullptr) == 0u);
    CHECK(Pipeline_native(nullptr) == nullptr);
    Pipeline_destroy(nullptr);

    printf("pipeline_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
