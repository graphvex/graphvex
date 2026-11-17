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
 * i32 blockCount                 initialized block directory entries
 * i32 blocksCap                  block-directory capacity
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
    i32 blockCount, blocksCap;
    any freeHead;             // intrusive free list over the slots
    u32 live;
};

;;GETTER
static bytes Pool_rawStride(const Pool *p) { return (*p).stride; }
;;SETTER
static fn Pool_setStride(Pool *p, bytes v) { (*p).stride = v; }
;;GETTER
static u32 Pool_blockCap(const Pool *p) { return (*p).blockCap; }
;;SETTER
static fn Pool_setBlockCap(Pool *p, u32 v) { (*p).blockCap = v; }
;;GETTER
static any *Pool_blocks(const Pool *p) { return (*p).blocks; }
;;SETTER
static fn Pool_setBlocks(Pool *p, any *v) { (*p).blocks = v; }
;;GETTER
static i32 Pool_blockCount(const Pool *p) { return (*p).blockCount; }
;;SETTER
static fn Pool_setBlockCount(Pool *p, i32 v) { (*p).blockCount = v; }
;;GETTER
static i32 Pool_blocksCap(const Pool *p) { return (*p).blocksCap; }
;;SETTER
static fn Pool_setBlocksCap(Pool *p, i32 v) { (*p).blocksCap = v; }
;;GETTER
static any Pool_freeHead(const Pool *p) { return (*p).freeHead; }
;;SETTER
static fn Pool_setFreeHead(Pool *p, any v) { (*p).freeHead = v; }
;;GETTER
static u32 Pool_rawLive(const Pool *p) { return (*p).live; }
;;SETTER
static fn Pool_setLive(Pool *p, u32 v) { (*p).live = v; }

// Appends a backing block pointer, growing the block directory when required.
static bool blocks_push(Pool *p, any block) {
    if (Pool_blockCount(p) == Pool_blocksCap(p)) {
        i32 cap = Pool_blocksCap(p) ? Pool_blocksCap(p) * 2 : 4;
        any *grown = realloc(Pool_blocks(p), (bytes)cap * sizeof *grown);
        if (!grown) return false;
        Pool_setBlocks(p, grown);
        Pool_setBlocksCap(p, cap);
    }
    Pool_blocks(p)[Pool_blockCount(p)] = block;
    Pool_setBlockCount(p, Pool_blockCount(p) + 1);
    return true;
}

// Allocates a block of fixed-stride slots and links them onto the intrusive free list.
static bool pool_grow(Pool *p) {
    bytes Bytes = Pool_rawStride(p) * (bytes)Pool_blockCap(p);
    u8 *mem = Memory_alloc(0, Bytes);
    if (!mem) return false;
    if (!blocks_push(p, mem)) { Memory_free(mem); return false; }
    for (u32 i = 0; i < Pool_blockCap(p); i++) {
        any slot = mem + (bytes)i * Pool_rawStride(p);
        *(any*) slot = Pool_freeHead(p);
        Pool_setFreeHead(p, slot);
    }
    return true;
}

// Creates a pool whose slots have the given stride and per-block capacity.
Pool *Pool_new(bytes stride, u32 blockCap) {
    if (stride < sizeof(any) || blockCap == 0) return nullptr;
    Pool *p = calloc(1, sizeof *p);
    if (!p) return nullptr;
    Pool_setStride(p, stride);
    Pool_setBlockCap(p, blockCap);
    return p;
}

// Returns every ForeignMemory block and releases the pool metadata.
fn Pool_destroy(Pool *pool) {
    if (!pool) return;
    for (i32 i = 0; i < Pool_blockCount(pool); i++) Memory_free(Pool_blocks(pool)[i]);
    free(Pool_blocks(pool));
    free(pool);
}

// Acquires and zeroes one slot, growing the pool when its free list is empty.
any Pool_alloc(Pool *pool) {
    if (!pool) return nullptr;
    if (!Pool_freeHead(pool) && !pool_grow(pool)) return nullptr;
    any item = Pool_freeHead(pool);
    Pool_setFreeHead(pool, *(any*) item);
    memset(item, 0, Pool_rawStride(pool));
    Pool_setLive(pool, Pool_rawLive(pool) + 1);
    return item;
}

// Returns a slot to the free list; caller must supply a live slot from this pool.
fn Pool_release(Pool *pool, any item) {
    if (!pool || !item) return;
    *(any*) item = Pool_freeHead(pool);
    Pool_setFreeHead(pool, item);
    Pool_setLive(pool, Pool_rawLive(pool) - 1);
}

// Checks whether an address lies within any allocated slot block (not whether it is slot-aligned).
bool Pool_contains(const Pool *pool, const_any item) {
    if (!pool || !item) return false;
    const u8 *p = item;
    for (i32 i = 0; i < Pool_blockCount(pool); i++) {
        const u8 *base = Pool_blocks(pool)[i];
        if (p >= base && p < base + Pool_rawStride(pool) * (bytes)Pool_blockCap(pool)) return true;
    }
    return false;
}

// Returns the number of currently allocated slots, or zero for null.
u32 Pool_live(const Pool *pool) { return pool ? Pool_rawLive(pool) : 0u; }
// Returns the configured slot stride in bytes, or zero for null.
bytes Pool_stride(const Pool *pool) { return pool ? Pool_rawStride(pool) : 0u; }
