#include "vex.h"
#include "graphics/image_runs.h"
#include "lang/image.h"

#include <assert.h>
#include <math.h>

// Owner: image_runs.{h,c}. Pixel-center nearest sampling, stride/stretch,
// clipped origin, transparent skip, callback failure and real raster colors.
// No texture upload, OOM injection, GPU execution, source crop or fit-mode proof.
typedef struct RunsState {
    u32 count;
    bool accept;
    Rect first;
    Color color;
} RunsState;

;;GETTER
static u32 RunsState_count(const RunsState *s) { return (*s).count; }
;;SETTER
static fn RunsState_setCount(RunsState *s, u32 count) { (*s).count = count; }
;;GETTER
static bool RunsState_accept(const RunsState *s) { return (*s).accept; }
;;SETTER
static fn RunsState_setFirst(RunsState *s, Rect first) { (*s).first = first; }
;;SETTER
static fn RunsState_setColor(RunsState *s, Color color) { (*s).color = color; }

// Records each visited image run and color into the supplied test state.
static bool record(Rect run, Color color, any userdata) {
    RunsState *state = userdata;
    if (!RunsState_count(state)) {
        RunsState_setFirst(state, run);
        RunsState_setColor(state, color);
    }
    RunsState_setCount(state, RunsState_count(state) + 1);
    return RunsState_accept(state);
}

// Tests run visitation, clipping, format rejection, and callback failure.
i32 main() {
    Image *image = Image_2(2, 1);
    assert(image && Image_ensureShadow(image, 2, 1));
    assert(Image_resize(image, 1, 1)); // retained stride exceeds visible row width
    Image_fill(image, COLOR_RGBA(12, 34, 56, 255));
    RunsState state = {.accept = true};
    assert(ImageRuns_visit(image, (Rect){-2, 1, 6, 2}, (Rect){0, 0, 8, 8}, record, &state));
    assert(state.count == 2 && state.first.x == 0 && state.first.y == 1 && state.first.w == 4);
    assert(state.color == COLOR_RGBA(12, 34, 56, 255));
    state = (RunsState){.accept = false};
    assert(!ImageRuns_visit(image, (Rect){0, 0, 2, 2}, (Rect){0, 0, 8, 8}, record, &state));
    assert(state.count == 1);
    state = (RunsState){.accept = true};
    Image_fill(image, COLOR_CLEAR);
    assert(ImageRuns_visit(image, (Rect){0, 0, 2, 2}, (Rect){0, 0, 8, 8}, record, &state));
    assert(state.count == 0);
    assert(!ImageRuns_visit(nullptr, (Rect){0, 0, 2, 2}, (Rect){0, 0, 8, 8}, record, &state));
    assert(!ImageRuns_visit(image, (Rect){NAN, 0, 2, 2}, (Rect){0, 0, 8, 8}, record, &state));
    assert(!ImageRuns_visit(image, (Rect){0, 0, 2, 2}, (Rect){0, 0, 8, 8}, nullptr, &state));
    assert(!ImageRuns_visit(image, (Rect){0, 0, 5000, 5000}, (Rect){0, 0, 5000, 5000}, record, &state));
    Image *bgra = Image_4(1, 1, IMAGE_FORMAT_BGRA8, IMAGE_USAGE_NONE);
    assert(bgra);
    assert(!ImageRuns_visit(bgra, (Rect){0, 0, 1, 1}, (Rect){0, 0, 8, 8}, record, &state));
    Image_destroy(bgra);

    assert(Image_resize(image, 2, 1));
    u8 *p = Image_pixels(image);
    p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255;
    p[4] = 0; p[5] = 255; p[6] = 0; p[7] = 255;
    assert(Graphics_use(BACKEND_RASTER) && Graphics_resize(8, 4));
    assert(Graphics_clear(COLOR_BLACK));
    Rect destination = {1, 1, 4, 2};
    assert(Graphics_drawImage(image, &destination));
    assert(Raster_pixelAt(1, 1) == COLOR_RGBA(255, 0, 0, 255));
    assert(Raster_pixelAt(2, 2) == COLOR_RGBA(255, 0, 0, 255));
    assert(Raster_pixelAt(3, 1) == COLOR_RGBA(0, 255, 0, 255));
    assert(Raster_pixelAt(5, 1) == COLOR_BLACK);
    Image_destroy(image);
    return 0;
}
