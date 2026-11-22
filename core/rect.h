#ifndef GRAPHVEX_CORE_RECT_H
#define GRAPHVEX_CORE_RECT_H

/** core/rect.h -- integer native-pixel rectangle [x0, x1) x [y0, y1). */

#include "vex.h"

;;CLASSNAME("GraphicsRect")

typedef struct GraphicsRect {
    i32 x0;
    i32 y0;
    i32 x1;
    i32 y1;
} GraphicsRect;

GraphicsRect GraphicsRect_0();
GraphicsRect GraphicsRect_4(i32 x0, i32 y0, i32 x1, i32 y1);
#define GraphicsRect(...) CONSTRUCTOR_DISPATCH(GraphicsRect, __VA_ARGS__)

bool GraphicsRect_isEmpty(GraphicsRect rect);
u32 GraphicsRect_width(GraphicsRect rect);
u32 GraphicsRect_height(GraphicsRect rect);
GraphicsRect GraphicsRect_intersect(GraphicsRect a, GraphicsRect b);

#endif
