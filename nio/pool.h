#ifndef GRAPHICS_NIO_POOL_H
#define GRAPHICS_NIO_POOL_H

#include "vex.h"

// graphvex R3 — nio/pool.h
//
// A growable fixed-stride slab pool. Two properties make it the right home for
// the placement vocabulary:
//
//   1. STABLE ADDRESSES. Growth APPENDS a block; a live item never moves, so an
//      Element may hold a borrowed pointer for the pool's whole life and share
//      it with other Elements (aliasing = the bind).
//   2. FOREIGN MEMORY. Blocks come from vexspoke's ForeignMemory (Memory_alloc),
//      not the C heap, so the pool is one arena teardown.
//
// No ceiling: a full block just adds another (the No Hardcoding Law).
typedef struct Pool Pool;

Pool *Pool_new(bytes stride, u32 blockCap);
fn Pool_destroy(Pool *pool);

any Pool_alloc(Pool *pool);              // a zeroed slot, or NULL on OOM
fn Pool_release(Pool *pool, any item); // return a slot to the free list
bool Pool_contains(const Pool *pool, const_any item);   // does the slot belong here?

u32 Pool_live(const Pool *pool);
bytes Pool_stride(const Pool *pool);

#endif // GRAPHICS_NIO_POOL_H
