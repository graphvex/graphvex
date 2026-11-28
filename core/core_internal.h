#ifndef GRAPHVEX_CORE_INTERNAL_H
#define GRAPHVEX_CORE_INTERNAL_H

/**
 * core/core_internal.h -- private Graphics context layout for core/core.c.
 */

#include "core/core.h"
#include "nio/graphvex_memory.h"

/**
 * Internal state for the unified Graphics command context.
 *
 * ImagePool *pool            - borrowed primary ImagePool
 * ImagePool *auxPools[]      - borrowed auxiliary domain pools (e.g. CompositorPool)
 * u32 auxPoolCount           - registered auxiliary pool count
 * GraphicsDriver driver      - active backend driver vtable
 * Image *target              - borrowed current render target Image
 * GraphicsRect scissor       - active clip rect in native pixels
 * bool scissorActive         - true when scissor is active
 * u64 frameIndex             - monotonic submitted frame count
 * u32 bindlessNextSlot       - next bindless image slot (1-based)
 * u32 shaderCount            - software shader counter
 * u32 swBackingNextSlot      - software backing slot counter
 * GraphicsCommand *commands  - owned command ring buffer
 * SecureHandle selfHandle    - sandcastles handle for Graphics
 * SecureHandle cmdHandle     - sandcastles handle for commands array
 * u32 commandCount           - queued command count
 * u32 commandCap             - allocated command capacity
 */
struct Graphics {
    ImagePool *pool;
    const ImagePool *auxPools[GRAPHICS_MAX_AUX_POOLS];
    u32 auxPoolCount;
    GraphicsDriver driver;
    Image *target;
    GraphicsRect scissor;
    bool scissorActive;
    u64 frameIndex;
    u32 bindlessNextSlot;
    u32 shaderCount;
    u32 swBackingNextSlot;
    GraphicsCommand *commands;
    SecureHandle selfHandle;
    SecureHandle cmdHandle;
    u32 commandCount;
    u32 commandCap;
};

#endif
