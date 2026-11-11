// Image-content seam for ui/element.{h,c}: ordinary paint order, stretch source,
// own rounded/ancestor clips, body-only effects and borrowed-image lifetime.
// Raster pixel checks require the real RGBA shadow draw backend, not a mock.
// No GPU, fit modes, filter application or asynchronous lifetime proof implied.
#include <assert.h>
#include <stdio.h>
#include "graphvex/lang/image.h"
#include "ui/element.h"

// Verifies image-backed elements paint, update, and reject invalid images.
int main(void) {
    const u8 rgba[] = {255, 0, 0, 255, 0, 255, 0, 255};
    Image *image = Image_2(2, 1);
    assert(image && Image_upload(rgba, 2, 1, image));
    ElementDesc desc = {.width = 8, .height = 4, .background = COLOR_BLACK};
    Element *root = Element(&desc);
    ElementDesc childDesc = {.width = 1, .height = 1, .offsetX = 4,
                             .offsetY = 1, .background = COLOR_WHITE};
    Element *child = Element(&childDesc);
    assert(root && child);
    Element_add(root, child);
    Element_revalidate(root);
    assert(!Element_isDirty(root));
    assert(Element_setImage(root, image) == root && Element_isDirty(root));
    assert(Element_image(root) == image && Element_image(child) == nullptr);
    assert(Element_image(nullptr) == nullptr && Element_setImage(nullptr, image) == nullptr);
    DisplayList *dl = DisplayList_0();
    assert(dl);
    Element_paint(root, (Rect){2, 2, 8, 4}, dl);
    const DrawCmd *commands = DisplayList_cmds(dl);
    assert(DisplayList_count(dl) == 3);
    assert(commands[0].kind == CMD_RECT && commands[1].kind == CMD_IMAGE);
    assert(commands[1].image == image && commands[2].kind == CMD_RECT);
    Rect source = commands[1].src, dest = commands[1].dst;
    assert(source.x == 0 && source.y == 0 && source.w == 2 && source.h == 1);
    assert(dest.x == 2 && dest.y == 2 && dest.w == 8 && dest.h == 4);
    assert(Graphics_register(RasterGraphics_row()) && Graphics_use(BACKEND_RASTER));
    assert(Raster_configure(16, 12) && Graphics_clear(COLOR_CLEAR));
    assert(Graphics_submit(dl));
    assert(Raster_pixelAt(3, 3) == COLOR_RGBA(255, 0, 0, 255));
    assert(Raster_pixelAt(8, 3) == COLOR_RGBA(0, 255, 0, 255));
    assert(Raster_pixelAt(6, 3) == COLOR_WHITE); // child paints after image
    Element_setRadius(root, 2);
    DisplayList_clear(dl);
    Element_paint(root, (Rect){2, 2, 8, 4}, dl);
    commands = DisplayList_cmds(dl);
    assert(commands[1].kind == CMD_CLIP_PUSH && commands[1].radius == 2);
    assert(commands[2].kind == CMD_IMAGE && commands[3].kind == CMD_CLIP_POP);
    assert(Graphics_clear(COLOR_CLEAR) && Graphics_submit(dl));
    assert(Color_alpha(Raster_pixelAt(2, 2)) < 255); // actual rounded mask, no square image corner
    Element_setRadius(root, 0);
    Element_setBackground(root, COLOR_CLEAR);
    Element_setClip(root, true);
    Element_setOffset(child, 7, 0);
    Element_setSize(child, 4, 4);
    DisplayList_clear(dl);
    Element_paint(root, (Rect){2, 2, 8, 4}, dl);
    assert(Graphics_clear(COLOR_CLEAR) && Graphics_submit(dl));
    assert(Raster_pixelAt(10, 3) == COLOR_CLEAR); // ancestor clips child overflow
    Element_setVisible(root, false);
    DisplayList_clear(dl);
    Element_paint(root, (Rect){2, 2, 8, 4}, dl);
    assert(DisplayList_count(dl) == 0);
    Element_setVisible(root, true);
    Element_setImage(root, nullptr);
    assert(Element_image(root) == nullptr && Image_pixels(image)[0] == 255);
    // Clearing recorded borrows precedes destroying the externally owned asset.
    DisplayList_free(dl);
    Element_destroy(root);
    Image_fill(image, COLOR_WHITE);
    assert(Image_pixels(image)[0] == 255 && Image_pixels(image)[1] == 255);
    Image_destroy(image);
    puts("element_image_test: PASS (normal paint path and Raster image pixels)");
    return 0;
}
