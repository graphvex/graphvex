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

/**
 * Recorded backend-agnostic GPU command carrying either GraphicsImage or
 * GraphicsBackingImage descriptors for target and source.
 *
 * GraphicsCommandKind kind - command opcode
 * u32 shaderId             - 1-based registered shader handle
 * Image target             - target Image header (Graphics or Backing)
 * vpointer targetData      - ImagePool GPU address when target is GraphicsImage
 * u32 targetBackingSlot    - driver backing slot when target is GraphicsBackingImage
 * Image source             - source Image header (Graphics or Backing)
 * vpointer sourceData      - ImagePool GPU address when source is GraphicsImage
 * u32 sourceBackingSlot    - driver backing slot when source is GraphicsBackingImage
 * GraphicsRect scissor     - clip rect in native pixels
 * u32 clearColorRGBA       - packed 0xRRGGBBAA clear color
 * i32 dstX                 - copy destination X offset
 * i32 dstY                 - copy destination Y offset
 * u32 gridX                - compute dispatch X threads
 * u32 gridY                - compute dispatch Y threads
 * u32 gridZ                - compute dispatch Z threads
 * u32 drawCount            - instanced primitive count
 * vpointer vertexAddr      - GPU address of vertex/instance buffer
 * vpointer indexAddr       - GPU address of optional index buffer
 * vpointer uniformAddr     - GPU address of uniform buffer
 * bytes uniformBytes       - uniform buffer byte size
 * u32 bindlessSlots[]      - bound image slots
 * any nativeSurface        - opaque OS surface handle
 */
typedef struct GraphicsCommand {
    GraphicsCommandKind kind;
    u32 shaderId;
    Image target;
    vpointer targetData;
    u32 targetBackingSlot;
    Image source;
    vpointer sourceData;
    u32 sourceBackingSlot;
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
