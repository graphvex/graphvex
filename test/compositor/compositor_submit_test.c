#include "vex.h"
#include "compositor/compositor_submit.h"
#include "../../compositor/compositor_image.h"
#include "graphvex/lang/image.h"

#include <assert.h>
#include <limits.h>

// Owns compositor_submit.{h,c}: null/empty, exact placement, borrowed output
// lifetime, blur halo and raster submission. No GPU, OOM
// injection, concurrency or end-to-end linear-light presentation proof.
// Exercises compositor submission recording and preserved-state failures.
int main() {
    DisplayList *list = DisplayList_0();

    assert(list);

    Image *source = Image_2(1, 1);

    assert(source);

    Image_fill(source, COLOR_WHITE);

    CompositorSurface *surface = nullptr;

    assert(CompositorSurface_fromImage(source, 5, 6, &surface) == COMPOSITOR_OK);

    const CompositorSurface *sources[] = {surface};

    FilterToken filter = Filter_scatterBlur(1);

    CompositorSurface *filtered = nullptr;

    assert(Compositor_compose(sources, 1, &filter, 1, &filtered) == COMPOSITOR_OK);

    Image *output = nullptr;

    assert(Compositor_record(filtered, list, &output) == COMPOSITOR_OK);

    assert(output && Image_width(output) == 3 && Image_height(output) == 3);

    assert(DisplayList_count(list) == 1);

    const DrawCmd *command = DisplayList_cmds(list);

    assert((*command).kind == CMD_IMAGE && (*command).image == output);

    Rect dst = (*command).dst;

    assert(dst.x == 4 && dst.y == 5);

    assert(dst.w == 3 && dst.h == 3);

    // Export detached the result: source surfaces may die before list submission.
    CompositorSurface_destroy(filtered);

    CompositorSurface_destroy(surface);

    Image_destroy(source);

    assert(Graphics_use(BACKEND_RASTER));

    assert(Graphics_resize(12, 12));

    assert(Graphics_clear(COLOR_BLACK));

    assert(Graphics_submit(list));

    assert(Raster_pixelAt(3, 6) == COLOR_BLACK);

    Color halo = Raster_pixelAt(4, 5);

    assert(Color_red(halo) == 28 && Color_green(halo) == 28 && Color_blue(halo) == 28);

    assert(Raster_pixelAt(7, 6) == COLOR_BLACK);

    DisplayList_clear(list);

    Image_destroy(output);

    CompositorSurface *empty = nullptr;

    assert(CompositorSurface_create((CompositorBounds){0}, &empty) == COMPOSITOR_OK);

    output = nullptr;

    assert(Compositor_record(empty, list, &output) == COMPOSITOR_INVALID);

    assert(!output && DisplayList_count(list) == 0);

    assert(Compositor_record(nullptr, list, &output) == COMPOSITOR_INVALID);

    assert(Compositor_record(empty, nullptr, &output) == COMPOSITOR_INVALID);

    assert(Compositor_record(empty, list, nullptr) == COMPOSITOR_INVALID);

    CompositorSurface_destroy(empty);

    CompositorSurface *far = nullptr;

    CompositorBounds bounds = {INT32_MAX - 2, 0, 1, 1};

    assert(CompositorSurface_create(bounds, &far) == COMPOSITOR_OK);

    assert(Compositor_record(far, list, &output) == COMPOSITOR_LIMIT);

    assert(!output && DisplayList_count(list) == 0);

    CompositorSurface_destroy(far);

    // Origin/extent can each be exact while their far edge is not binary32-exact.
    bounds = (CompositorBounds){16777216, 0, 1, 1};

    assert(CompositorSurface_create(bounds, &far) == COMPOSITOR_OK);

    assert(Compositor_record(far, list, &output) == COMPOSITOR_LIMIT);

    assert(!output && DisplayList_count(list) == 0);

    CompositorSurface_destroy(far);

    DisplayList_free(list);

    return 0;
}
