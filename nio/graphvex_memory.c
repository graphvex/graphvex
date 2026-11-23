#include "vex.h"
#include "nio/graphvex_memory.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GraphvexMemory (graphvex/api/nio/graphvex_memory.c)
 * ============================================================================
 * Per-library MemoryArena owners for Graphvex API (general, geometry, GPU
 * buffers, and shaders), backed by Relational Engine's MemoryPool. Each arena
 * starts at zero OS pages and grows on demand until OS OOM.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: GraphvexMemory (nio/graphvex_memory)
 * ============================================================================
 * Lazily creates and shuts down Graphvex API's domain MemoryArenas.
 * ============================================================================
 */

static MemoryArena *s_graphvexArena = nullptr;
static MemoryArena *s_geomArena = nullptr;
static MemoryArena *s_bufferArena = nullptr;
static MemoryArena *s_shaderArena = nullptr;

/** Lazily create and return Graphvex API's default page-pooled MemoryArena. */
MemoryArena *Graphvex_arena() {
    if (s_graphvexArena == nullptr)
        s_graphvexArena = MemoryArena();
    return s_graphvexArena;
}

/** Lazily create and return Graphvex API's geometry MemoryArena. */
MemoryArena *Graphvex_geomArena() {
    if (s_geomArena == nullptr)
        s_geomArena = MemoryArena();
    return s_geomArena;
}

/** Lazily create and return Graphvex API's buffer MemoryArena. */
MemoryArena *Graphvex_bufferArena() {
    if (s_bufferArena == nullptr)
        s_bufferArena = MemoryArena();
    return s_bufferArena;
}

/** Lazily create and return Graphvex API's shader MemoryArena. */
MemoryArena *Graphvex_shaderArena() {
    if (s_shaderArena == nullptr)
        s_shaderArena = MemoryArena();
    return s_shaderArena;
}

/** Destroy all Graphvex API MemoryArenas and return their pages to MemoryPool. */
fn Graphvex_arenaShutdown() {
    if (s_shaderArena != nullptr) {
        MemoryArena_destroy(s_shaderArena);
        s_shaderArena = nullptr;
    }
    if (s_bufferArena != nullptr) {
        MemoryArena_destroy(s_bufferArena);
        s_bufferArena = nullptr;
    }
    if (s_geomArena != nullptr) {
        MemoryArena_destroy(s_geomArena);
        s_geomArena = nullptr;
    }
    if (s_graphvexArena != nullptr) {
        MemoryArena_destroy(s_graphvexArena);
        s_graphvexArena = nullptr;
    }
}
