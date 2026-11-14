#include "vex.h"
#include "nio/pool.h"

#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"   // vexspoke R2 — ForeignMemory (Memory_alloc / Memory_free)

;;DEFINITION
/** Pool owns a growable directory of fixed-stride blocks allocated through the
 * Relational Engine-backed Memory ABI. Blocks append without moving earlier
 * slots; a free list is threaded through slot storage itself. Destroy requires
 * all borrowers to be excluded and invalidates every returned slot pointer. */
;;OVERVIEW
/** ========================================================================
 * CLASS: Pool (nio/pool.c)
 * STRUCT FIELDS (Mirroring Pool)
 * bytes stride                  bytes in each fixed-size slot
 * u32 blockCap              slots admitted per backing block
 * any *blocks                  owned directory of Memory_alloc blocks
 * int blockCount                 initialized block directory entries
 * int blocksCap                  block-directory capacity
 * any freeHead                 head of intrusive free-slot chain
 * u32 live                  currently checked-out slot count
 * FUNCTION REGISTRY
 * Constructors: Pool *Pool_new(bytes stride, u32 blockCap)
 * Lifecycle: fn Pool_destroy(Pool *pool)
 * Operations: any Pool_alloc(Pool *pool), fn Pool_release(Pool *pool,
 *   any item), bool Pool_contains(const Pool *pool, const_any item)
 * Projections: u32 Pool_live(const Pool *pool),
 *   bytes Pool_stride(const Pool *pool)
 * Private helpers: blocks_push grows the block directory; pool_grow appends
 *   Memory-backed storage and links each slot into the free list.
 * Admission, failure and ownership contract:
 * Invalid geometry or allocation failure returns nullptr/false. Returned slots
 * are pool-owned storage borrowed by consumers until release or destroy;
 * release requires a live slot belonging to this pool. contains checks block
 * range membership, not slot alignment or live allocation identity.
 * ======================================================================== */

struct Pool {
    bytes stride;
    u32 blockCap;          // slots per block
    any *blocks;              // every ForeignMemory block, for teardown
    int blockCount, blocksCap;
    any freeHead;             // intrusive free list over the slots
    u32 live;
};

// Appends a backing block pointer, growing the block directory when required.
static bool blocks_push(Pool *p, any block) {
    if ((*p).blockCount == (*p).blocksCap) {
        int cap = (*p).blocksCap ? (*p).blocksCap * 2 : 4;
        any *grown = realloc((*p).blocks, (bytes)cap * sizeof *grown);
        if (!grown) return false;
        (*p).blocks = grown;
        (*p).blocksCap = cap;
    }
    (*p).blocks[(*p).blockCount++] = block;
    return true;
}

// Allocates a block of fixed-stride slots and links them onto the intrusive free list.
static bool pool_grow(Pool *p) {
    bytes Bytes = (*p).stride * (bytes)(*p).blockCap;
    u8 *mem = Memory_alloc(0, Bytes);
    if (!mem) return false;
    if (!blocks_push(p, mem)) { Memory_free(mem); return false; }
    for (u32 i = 0; i < (*p).blockCap; i++) {
        any slot = mem + (bytes)i * (*p).stride;
        *(any*) slot = (*p).freeHead;
        (*p).freeHead = slot;
    }
    return true;
}

// Creates a pool whose slots have the given stride and per-block capacity.
Pool *Pool_new(bytes stride, u32 blockCap) {
    if (stride < sizeof(any) || blockCap == 0) return nullptr;
    Pool *p = calloc(1, sizeof *p);
    if (!p) return nullptr;
    (*p).stride = stride;
    (*p).blockCap = blockCap;
    return p;
}

// Returns every ForeignMemory block and releases the pool metadata.
fn Pool_destroy(Pool *pool) {
    if (!pool) return;
    for (int i = 0; i < (*pool).blockCount; i++) Memory_free((*pool).blocks[i]);
    free((*pool).blocks);
    free(pool);
}

// Acquires and zeroes one slot, growing the pool when its free list is empty.
any Pool_alloc(Pool *pool) {
    if (!pool) return nullptr;
    if (!(*pool).freeHead && !pool_grow(pool)) return nullptr;
    any item = (*pool).freeHead;
    (*pool).freeHead = *(any*) item;
    memset(item, 0, (*pool).stride);
    (*pool).live++;
    return item;
}

// Returns a slot to the free list; caller must supply a live slot from this pool.
fn Pool_release(Pool *pool, any item) {
    if (!pool || !item) return;
    *(any*) item = (*pool).freeHead;
    (*pool).freeHead = item;
    (*pool).live--;
}

// Checks whether an address lies within any allocated slot block (not whether it is slot-aligned).
bool Pool_contains(const Pool *pool, const_any item) {
    if (!pool || !item) return false;
    const u8 *p = item;
    for (int i = 0; i < (*pool).blockCount; i++) {
        const u8 *base = (*pool).blocks[i];
        if (p >= base && p < base + (*pool).stride * (bytes)(*pool).blockCap) return true;
    }
    return false;
}

// Returns the number of currently allocated slots, or zero for null.
u32 Pool_live(const Pool *pool) { return pool ? (*pool).live : 0u; }
// Returns the configured slot stride in bytes, or zero for null.
bytes Pool_stride(const Pool *pool) { return pool ? (*pool).stride : 0u; }
