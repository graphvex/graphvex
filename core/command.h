#ifndef GRAPHVEX_CORE_COMMAND_H
#define GRAPHVEX_CORE_COMMAND_H

/** core/command.h -- unified, backend-agnostic GPU command record. */

#include "core/image.h"
#include "core/rect.h"

;;CLASSNAME("GraphicsCommand")

typedef enum GraphicsCommandKind {
    GRAPHICS_CMD_NONE     = 0,
    GRAPHICS_CMD_CLEAR    = 1,
    GRAPHICS_CMD_COPY     = 2,
    GRAPHICS_CMD_DISPATCH = 3,
    GRAPHICS_CMD_DRAW     = 4,
    GRAPHICS_CMD_PRESENT  = 5,
} GraphicsCommandKind;

typedef struct GraphicsCommand {
    GraphicsCommandKind kind;
    u32 shaderId;
    GraphicsImage target;
    GraphicsImage source;
    GraphicsRect scissor;
    u32 clearColorRGBA;
    i32 dstX;
    i32 dstY;
    u32 gridX;
    u32 gridY;
    u32 gridZ;
    u32 drawCount;
    vpointer vertexAddr;
    vpointer indexAddr;
    vpointer uniformAddr;
    bytes uniformBytes;
    u32 bindlessSlots[GRAPHICS_MAX_BINDINGS];
    any nativeSurface;
} GraphicsCommand;

#endif
