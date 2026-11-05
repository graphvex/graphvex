/* Owner: filter/filter_type.h. Pure registry: exact legacy values, unique IDs
 * for every declared effect, fit in ID16, distinct recipe operation. No pixels,
 * allocation, callbacks, pointer validation or concurrency state to exercise. */
#include "filter/filter_type.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

// Checks canonical filter IDs, aliases, and operation-table consistency.
int main(void) {
    const uint32_t ids[] = {
        IDENTITY_ID, GAIN_ID, SCATTER_BLUR_ID, BRIGHTNESS_ID,
        CONTRAST_ID, TONE_CURVE_ID, HSL_ID, HSV_ID,
        COLOR_BALANCE_ID, EDGES_ID, DROP_SHADOW_ID, MONOCOLOR_ID,
        GRAYSCALE_ID, GRAYSCALE_RED_ID, GRAYSCALE_GREEN_ID,
        GRAYSCALE_BLUE_ID, BLACK_AND_WHITE_ID, INVERT_ID,
        GRADIENT_MAP_ID, REPLACE_COLOR_ID, GAUSSIAN_BLUR_ID,
        BOX_BLUR_ID, ZOOMING_BLUR_ID, MOVING_BLUR_ID, SPIN_BLUR_ID,
        LENS_BLUR_ID, MOSAIC_ID, UNSHARP_MASK_ID, FROSTED_GLASS_ID,
        STROKE_ID, STAINED_GLASS_ID, OUTER_GLOW_ID, INNER_GLOW_ID,
        EMBOSS_ID, RELIEF_ID, WATERDROP_ID, EXTRUDE_ID,
        GOD_RAYS_ID, CHROMATIC_ABERRATION_ID, GLITCH_ID, NOISE_ID,
        DITHERING_ID, CHROME_ID, BLOOM_ID, SHEER_ID,
        PIXELATE_ID, POINTILLIZE_ID, EXPANSION_ID, FISHEYE_ID,
        SPHERE_ID, WAVE_ID, DOTS_ID, RECIPE_POOL_ID
    };
    const size_t count = sizeof ids / sizeof ids[0];
    assert(count == 53);
    assert(FILTER_IDENTITY == 0 && FILTER_GAIN == 1 && FILTER_SCATTER_BLUR == 2);
    assert(FILTER_POOL_ID == 0x8000);
    assert(FILTER_IDENTITY == IDENTITY_ID && FILTER_GAIN == GAIN_ID);
    assert(FILTER_SCATTER_BLUR == SCATTER_BLUR_ID && FILTER_POOL_ID == RECIPE_POOL_ID);
    for (size_t i = 0; i < count; ++i) {
        assert(ids[i] <= UINT16_MAX);
        if (i + 1 < count)
            assert(ids[i] == i);
        for (size_t j = i + 1; j < count; ++j)
            assert(ids[i] != ids[j]);
    }
    puts("filter operation registry: PASS");
    return 0;
}
