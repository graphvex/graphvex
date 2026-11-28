#include "vex.h"
#include "nio/graphvex_memory.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GraphvexMemory (graphvex/api/nio/graphvex_memory.c)
 * ============================================================================
 * Per-library MemoryArena owners and isolated SecureMemoryPool /
 * SecureMemoryArena owners for Graphvex API. CPU-side graphics metadata and
 * shader blobs live inside Graphvex_secureArena() (guard-paged, zero-on-alloc,
 * zero-on-free) so graphics allocations never mix with general CPU arenas.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: GraphvexMemory (nio/graphvex_memory)
 * ============================================================================
 * Lazily creates and shuts down Graphvex API's domain arenas and isolated
 * SecureMemoryPool / SecureMemoryArena.
 * ============================================================================
 */

static MemoryArena *s_graphvexArena = nullptr;
static MemoryArena *s_geomArena = nullptr;
static MemoryArena *s_bufferArena = nullptr;
static MemoryArena *s_shaderArena = nullptr;
static SecureMemoryPool *s_securePool = nullptr;
static SecureMemoryArena *s_secureArena = nullptr;

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

static constexpr bytes GRAPHVEX_SECURE_POOL_PAGES = 128;

/** Lazily create and return Graphvex API's isolated guard-paged SecureMemoryPool. */
SecureMemoryPool *Graphvex_securePool() {
    if (s_securePool == nullptr)
        s_securePool = SecureMemoryPool(GRAPHVEX_SECURE_POOL_PAGES);
    return s_securePool;
}

/** Lazily create and return Graphvex API's isolated SecureMemoryArena. */
SecureMemoryArena *Graphvex_secureArena() {
    if (s_secureArena == nullptr) {
        SecureMemoryPool *pool = Graphvex_securePool();
        if (pool != nullptr)
            s_secureArena = SecureMemoryArena(pool, GRAPHVEX_SECURE_POOL_PAGES);
    }
    return s_secureArena;
}

/** Destroy all Graphvex API arenas and scrub/unmap the isolated SecureMemoryPool. */
fn Graphvex_arenaShutdown() {
    if (s_secureArena != nullptr) {
        SecureMemoryArena_destroy(s_secureArena);
        s_secureArena = nullptr;
    }
    if (s_securePool != nullptr) {
        SecureMemoryPool_destroy(s_securePool);
        s_securePool = nullptr;
    }
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
