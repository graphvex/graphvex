#include "nio/property_pool.h"
#include "util/annotation.h"

#include <stdlib.h>

#include "nio/pool.h"

;;DEFINITION
/** PropertyPool is the typed stable-address owner for Graphvex Property records.
 * It delegates storage to Pool and copies either caller-provided initial state
 * or Property_default into each admitted slot. The default instance is a
 * process-global borrowed pool and must not be destroyed by consumers. */
;;OVERVIEW
/** ========================================================================
 * CLASS: PropertyPool (nio/property_pool.c)
 * STRUCT FIELDS (Mirroring PropertyPool)
 * Pool *slots                    fixed-stride storage and live-slot accounting
 * FUNCTION REGISTRY
 * Constructors: PropertyPool *PropertyPool_0()
 *   PropertyPool *PropertyPool_default()
 * Lifecycle: fn PropertyPool_destroy(PropertyPool *pool)
 * Operations: Property *PropertyPool_alloc(PropertyPool *pool,
 *   const Property *init), fn PropertyPool_release(PropertyPool *pool,
 *   Property *property)
 * Projections: u32 PropertyPool_live(const PropertyPool *pool)
 * Private configuration: PROPERTY_POOL_BLOCK_CAP is the initial per-block
 *   capacity; Pool grows by appending blocks.
 * Null/default, failure and ownership contract:
 * Allocation returns nullptr on invalid owner or backing failure. A successful
 * Property pointer is borrowed from the pool and stable until release/destroy.
 * The default pool is process-global and callers only borrow it.
 * ======================================================================== */

#define PROPERTY_POOL_BLOCK_CAP 64u

struct PropertyPool {
    Pool *slots;
};

// Creates a typed pool for stable Property records.
PropertyPool *PropertyPool_0() {
    PropertyPool *pp = calloc(1, sizeof *pp);
    if (!pp) return nullptr;
    (*pp).slots = Pool_new(sizeof(Property), PROPERTY_POOL_BLOCK_CAP);
    if (!(*pp).slots) { free(pp); return nullptr; }
    return pp;
}

// The process-global pool Element-owned bounds come from (like memory's default
// arena). Borrowed, never destroyed by callers.
static PropertyPool *s_default = nullptr;
// Returns the process-global lazily created pool; callers borrow it and must not destroy it.
PropertyPool *PropertyPool_default() {
    if (!s_default) s_default = PropertyPool_0();
    return s_default;
}

// Destroys the pool and all of its backing storage; do not use on the shared default pool.
fn PropertyPool_destroy(PropertyPool *pool) {
    if (!pool) return;
    Pool_destroy((*pool).slots);
    free(pool);
}

// Allocates a stable property slot and copies init, or the default property when init is null.
Property *PropertyPool_alloc(PropertyPool *pool, const Property *init) {
    if (!pool) return nullptr;
    Property *p = Pool_alloc((*pool).slots);
    if (!p) return nullptr;
    *p = init ? *init : Property_default();
    return p;
}

// Returns a property slot to its pool; the pointer must belong to that pool.
fn PropertyPool_release(PropertyPool *pool, Property *property) {
    if (pool && property) Pool_release((*pool).slots, property);
}

// Returns the number of outstanding property slots, or zero for null.
u32 PropertyPool_live(const PropertyPool *pool) {
    return pool ? Pool_live((*pool).slots) : 0u;
}
