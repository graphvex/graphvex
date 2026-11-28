#ifndef GRAPHVEX_IMAGE_MEMORY_H
#define GRAPHVEX_IMAGE_MEMORY_H

/** nio/image_memory.h -- lazy MemoryArena ownership for graphvex/api/image. */

#include "vex.h"
#include "nio/mem.h"

MemoryArena *Image_arena();
fn Image_shutdown();

#endif
