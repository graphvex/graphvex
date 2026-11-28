#include "vex.h"
#include "image/nio/image_memory.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ImageMemory (graphvex/api/image/nio/image_memory.c)
 * ============================================================================
 * Owns graphvex/api/image's lazy CPU MemoryArena used for decoded RGBA8 staging
 * buffers, metadata scratch allocations, and format conversion intermediates.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ImageMemory (nio/image_memory)
 * ============================================================================
 * Lazily initializes and tears down graphvex/api/image's CPU MemoryArena.
 *
 * --FUNCTIONS----------------------------
 * public   Image_arena       returns lazy MemoryArena for graphvex/api/image
 * public   Image_shutdown    destroys graphvex/api/image MemoryArena
 * ============================================================================
 */

static MemoryArena *s_imageArena = nullptr;

/**
 * Lazily creates and returns graphvex/api/image's CPU MemoryArena.
 * @return owned process-lifetime MemoryArena pointer
 */
MemoryArena *Image_arena()
{
    if (s_imageArena == nullptr)
        s_imageArena = MemoryArena();

    return s_imageArena;
}

/**
 * Destroys graphvex/api/image's CPU MemoryArena if initialized.
 */
fn Image_shutdown()
{
    if (s_imageArena != nullptr) {
        MemoryArena_destroy(s_imageArena);
        s_imageArena = nullptr;
    }
}
