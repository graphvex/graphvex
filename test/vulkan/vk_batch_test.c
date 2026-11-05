// graphvex/test/vulkan/vk_batch_test.c — mirrors src/vulkan/vk_batch.c
//
// The quad batcher: pure CPU data, so it is fully testable headless. Every
// rect/image/glyph becomes one quad = two triangles = six vertices.

#include <stdio.h>

#include "vulkan/vk_batch.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// Checks Vulkan batch recording, geometry validation, and image admission.
int main(void) {
    VkBatch *b = VkBatch();
    CHECK(b != nullptr);
    CHECK((*b).count == 0);
    CHECK(VkBatch_vertices(b, nullptr, 0) == 0);

    // a rect appends one quad; an empty rect is ignored
    VkBatch_rect(b, (Rect){1, 2, 3, 4}, &(Brush){COLOR_WHITE, 0, 0, 0, 0});
    CHECK((*b).count == 1);
    VkBatch_rect(b, (Rect){0, 0, 0, 5}, &(Brush){COLOR_WHITE, 0, 0, 0, 0});
    CHECK((*b).count == 1);
    VkBatch_rect(b, (Rect){0, 0, 5, 0}, nullptr);   // null brush ignored
    CHECK((*b).count == 1);

    // a glyph appends a mode-2 quad with its atlas layer
    VkBatch_glyph(b, (Rect){0, 0, 8, 8}, 2, COLOR_BLACK);
    CHECK((*b).count == 2);

    // 6 vertices per quad; the query never writes when capacity is short
    CHECK(VkBatch_vertices(b, nullptr, 0) == 12);
    VkVertex verts[12];
    CHECK(VkBatch_vertices(b, verts, 5) == 12);
    CHECK(VkBatch_vertices(b, verts, 12) == 12);

    // first quad corners (two triangles) map to the rect
    CHECK(verts[0].x == 1.0f && verts[0].y == 2.0f);
    CHECK(verts[1].x == 4.0f && verts[1].y == 2.0f);
    CHECK(verts[2].x == 1.0f && verts[2].y == 6.0f);
    CHECK(verts[5].x == 4.0f && verts[5].y == 6.0f);
    // fill normalized to 0..1; mode 0 = solid rect
    CHECK(verts[0].r == 1.0f && verts[0].g == 1.0f && verts[0].b == 1.0f && verts[0].a == 1.0f);
    CHECK(verts[0].mode == 0.0f);
    CHECK(verts[0].qw == 3.0f && verts[0].qh == 4.0f);
    // glyph vertex block: mode 2, layer 2
    CHECK(verts[6].mode == 2.0f);
    CHECK(verts[6].layer == 2.0f);

    // an image uses its native size for UVs and its layer for the sampler
    Image *img = Image_2(16, 16);
    Image_setLayer(img, 1);
    VkBatch_image(b, img, (Rect){0, 0, 16, 16}, (Rect){0, 0, 32, 32});
    CHECK((*b).count == 3);
    CHECK(VkBatch_vertices(b, nullptr, 0) == 18);
    VkVertex v3[18];
    CHECK(VkBatch_vertices(b, v3, 18) == 18);
    CHECK(v3[12].u == 0.0f && v3[12].v == 0.0f);
    CHECK(v3[13].u == 1.0f && v3[13].v == 0.0f);   // full 16x16 source
    CHECK(v3[12].mode == 1.0f);
    CHECK(v3[12].layer == 1.0f);
    Image_destroy(img);

    // null image / null batch are no-ops
    VkBatch_image(b, nullptr, (Rect){0, 0, 1, 1}, (Rect){0, 0, 1, 1});
    CHECK((*b).count == 3);
    VkBatch_rect(nullptr, (Rect){0, 0, 1, 1}, &(Brush){COLOR_WHITE, 0, 0, 0, 0});

    // clear resets; free is null-safe
    VkBatch_clear(b);
    CHECK((*b).count == 0);
    VkBatch_free(b);
    VkBatch_free(nullptr);

    printf("vk_batch_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
