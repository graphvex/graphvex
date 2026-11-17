#include "vex.h"
#include "compositor/filter_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/**
 * FilterPool stores complex ordered recipes as immutable inline-token records.
 * Tokens carry pool-local index/generation, not native pointers. Refcounts keep
 * records alive for CPU submission; externally synchronized synchronous compose
 * snapshots them before invoking the CPU compositor. Retire at generation
 * saturation: no old token ever becomes valid again within this pool lifetime.
 * Fixed cold allocations are bounded prototype storage, not arena/GPU lifetime
 * or zero-allocation compositor claims. No nested pooled recipes are admitted.
 */
;;OVERVIEW
/**
 * CLASS: FilterPool. Fields: capacity, maxRecipeFilters, live, slots.
 * Private Slot record: references, generation, count, retired, tokens[256].
 * Public: _2 constructor/macro, destroy, insert, retain, release, snapshot,
 * compose, capacity/maxRecipeFilters/live queries, toString/toStringStruct.
 * Private: lookup checks index/generation; reject reports cold errors once;
 * format projects bounded strings. No mutable recipe view is exported.
 */
typedef struct Slot {
    u32 references;
    u16 generation;
    bytes count;
    bool retired;
    FilterToken tokens[COMPOSITOR_MAX_FILTERS];
} Slot;

;;GETTER
static inline u32 Slot_references(const Slot *self) { return (*self).references; }
;;SETTER
static inline fn Slot_setReferences(Slot *self, u32 v) { (*self).references = v; }
;;GETTER
static inline u16 Slot_generation(const Slot *self) { return (*self).generation; }
;;SETTER
static inline fn Slot_setGeneration(Slot *self, u16 v) { (*self).generation = v; }
;;GETTER
static inline bytes Slot_count(const Slot *self) { return (*self).count; }
;;SETTER
static inline fn Slot_setCount(Slot *self, bytes v) { (*self).count = v; }
;;GETTER
static inline bool Slot_retired(const Slot *self) { return (*self).retired; }
;;SETTER
static inline fn Slot_setRetired(Slot *self, bool v) { (*self).retired = v; }
;;GETTER
static inline FilterToken *Slot_tokensMut(Slot *self) { return (*self).tokens; }
;;GETTER
static inline const FilterToken *Slot_tokens(const Slot *self) { return (*self).tokens; }

_Static_assert(FILTER_POOL_MAX_CAPACITY <= SIZE_MAX / sizeof(Slot),
               "configured slot budget must fit allocation size");

struct FilterPool {
    u32 capacity;
    u32 maxRecipeFilters;
    u32 live;
    Slot *slots;
};

;;SETTER
static inline fn FilterPool_setCapacity(FilterPool *self, u32 v) { (*self).capacity = v; }
;;SETTER
static inline fn FilterPool_setMaxRecipeFilters(FilterPool *self, u32 v) { (*self).maxRecipeFilters = v; }
;;SETTER
static inline fn FilterPool_setLive(FilterPool *self, u32 v) { (*self).live = v; }
;;GETTER
static inline Slot *FilterPool_slots(const FilterPool *self) { return (*self).slots; }
;;SETTER
static inline fn FilterPool_setSlots(FilterPool *self, Slot *v) { (*self).slots = v; }

// Reports a rejected cold operation and returns its status unchanged.
static FilterPoolStatus reject(FilterPoolStatus status) {
    if (status != FILTER_POOL_OK)
        THROW("FilterPool rejected operation: status=%d", (i32) status);
    return status;
}

// Resolves a live pool token after validating pool identity, slot index, and generation.
static FilterPoolStatus lookup(const FilterPool *pool, FilterToken token,
                               Slot **out) {
    if (!pool || Filter_id(token) != FILTER_POOL_ID)
        return FILTER_POOL_INVALID;
    u64 payload = Filter_payload(token);
    u32 index = (u32) payload;
    u16 generation = (u16) (payload >> 32);
    if (!generation)
        return FILTER_POOL_INVALID;
    if (index >= FilterPool_capacity(pool))
        return FILTER_POOL_STALE;
    Slot *slot = &FilterPool_slots(pool)[index];
    if (!Slot_references(slot) || Slot_generation(slot) != generation)
        return FILTER_POOL_STALE;
    *out = slot;
    return FILTER_POOL_OK;
}

// Creates bounded recipe storage and initializes each slot's first generation.
FilterPoolStatus FilterPool_2(FilterPoolConfig config, FilterPool **out) {
    if (!out || !config.capacity || !config.maxRecipeFilters)
        return reject(FILTER_POOL_INVALID);
    if (config.capacity > FILTER_POOL_MAX_CAPACITY ||
        config.maxRecipeFilters > COMPOSITOR_MAX_FILTERS)
        return reject(FILTER_POOL_LIMIT);
    FilterPool *pool = calloc(1, sizeof *pool);
    if (!pool)
        return reject(FILTER_POOL_NO_MEMORY);
    Slot *slots = calloc(config.capacity, sizeof *slots);
    if (!slots) {
        free(pool);
        return reject(FILTER_POOL_NO_MEMORY);
    }
    FilterPool_setCapacity(pool, config.capacity);
    FilterPool_setMaxRecipeFilters(pool, config.maxRecipeFilters);
    FilterPool_setSlots(pool, slots);
    for (u32 i = 0; i < config.capacity; ++i)
        slots[i].generation = 1;
    *out = pool;
    return FILTER_POOL_OK;
}

// Destroys an empty pool; returns busy and preserves it while recipes remain live.
FilterPoolStatus FilterPool_destroy(FilterPool *pool) {
    if (!pool)
        return FILTER_POOL_OK;
    if (FilterPool_live(pool))
        return reject(FILTER_POOL_BUSY);
    free(FilterPool_slots(pool));
    free(pool);
    return FILTER_POOL_OK;
}

// Validates and copies an ordered non-nested recipe into an available slot.
FilterPoolStatus FilterPool_insert(FilterPool *pool, const FilterToken *tokens,
                                  bytes count, FilterToken *out) {
    if (!pool || !out || (count && !tokens))
        return reject(FILTER_POOL_INVALID);
    if (count > FilterPool_maxRecipeFilters(pool))
        return reject(FILTER_POOL_LIMIT);
    for (bytes i = 0; i < count; ++i)
        if (Filter_id(tokens[i]) == FILTER_POOL_ID)
            return reject(FILTER_POOL_UNSUPPORTED);
    CompositorBounds unused;
    CompositorStatus status = Compositor_filterBounds((CompositorBounds){0},
                                                     tokens, count, &unused);
    if (status != COMPOSITOR_OK)
        return reject((FilterPoolStatus) status);
    for (u32 i = 0; i < FilterPool_capacity(pool); ++i) {
        Slot *slot = &FilterPool_slots(pool)[i];
        if (Slot_references(slot) || Slot_retired(slot))
            continue;
        if (count)
            memcpy(Slot_tokensMut(slot), tokens, count * sizeof *tokens);
        Slot_setCount(slot, count);
        Slot_setReferences(slot, 1);
        FilterPool_setLive(pool, FilterPool_live(pool) + 1);
        *out = ((u64) FILTER_POOL_ID << 48) |
               ((u64) Slot_generation(slot) << 32) | i;
        return FILTER_POOL_OK;
    }
    return reject(FILTER_POOL_LIMIT);
}

// Adds one reference to a live recipe unless its reference count is saturated.
FilterPoolStatus FilterPool_retain(FilterPool *pool, FilterToken token) {
    Slot *slot;
    FilterPoolStatus status = lookup(pool, token, &slot);
    if (status != FILTER_POOL_OK)
        return reject(status);
    if (Slot_references(slot) == UINT32_MAX)
        return reject(FILTER_POOL_LIMIT);
    Slot_setReferences(slot, Slot_references(slot) + 1);
    return FILTER_POOL_OK;
}

// Drops one recipe reference and advances or retires its slot generation at zero.
FilterPoolStatus FilterPool_release(FilterPool *pool, FilterToken token) {
    Slot *slot;
    FilterPoolStatus status = lookup(pool, token, &slot);
    if (status != FILTER_POOL_OK)
        return reject(status);
    u32 nextRefs = Slot_references(slot) - 1;
    Slot_setReferences(slot, nextRefs);
    if (nextRefs)
        return FILTER_POOL_OK;
    FilterPool_setLive(pool, FilterPool_live(pool) - 1);
    Slot_setCount(slot, 0);
    if (Slot_generation(slot) == UINT16_MAX)
        Slot_setRetired(slot, true);
    else
        Slot_setGeneration(slot, Slot_generation(slot) + 1);
    return FILTER_POOL_OK;
}

// Copies a live recipe into caller storage, leaving output count untouched on rejection.
FilterPoolStatus FilterPool_snapshot(const FilterPool *pool, FilterToken token,
                                    FilterToken *outTokens, bytes capacity,
                                    bytes *outCount) {
    Slot *slot;
    FilterPoolStatus status = lookup(pool, token, &slot);
    if (status != FILTER_POOL_OK)
        return reject(status);
    bytes count = Slot_count(slot);
    if (!outCount || (count && !outTokens))
        return reject(FILTER_POOL_INVALID);
    if (capacity < count)
        return reject(FILTER_POOL_LIMIT);
    if (count)
        memcpy(outTokens, Slot_tokens(slot), count * sizeof *outTokens);
    *outCount = count;
    return FILTER_POOL_OK;
}

// Expands pool tokens in order and composes the supplied source group with that recipe.
FilterPoolStatus FilterPool_compose(const FilterPool *pool,
                                   const CompositorSurface *const *sources,
                                   bytes sourceCount, const FilterToken *tokens,
                                   bytes tokenCount, CompositorSurface **out) {
    if (!pool || !out || (tokenCount && !tokens) || (sourceCount && !sources))
        return reject(FILTER_POOL_INVALID);
    if (tokenCount > COMPOSITOR_MAX_FILTERS || sourceCount > COMPOSITOR_MAX_SOURCES)
        return reject(FILTER_POOL_LIMIT);
    FilterToken expanded[COMPOSITOR_MAX_FILTERS];
    bytes count = 0;
    for (bytes i = 0; i < tokenCount; ++i) {
        if (Filter_id(tokens[i]) != FILTER_POOL_ID) {
            if (count == COMPOSITOR_MAX_FILTERS)
                return reject(FILTER_POOL_LIMIT);
            expanded[count++] = tokens[i];
            continue;
        }
        Slot *slot;
        FilterPoolStatus status = lookup(pool, tokens[i], &slot);
        if (status != FILTER_POOL_OK)
            return reject(status);
        bytes slotCount = Slot_count(slot);
        if (slotCount > COMPOSITOR_MAX_FILTERS - count)
            return reject(FILTER_POOL_LIMIT);
        if (slotCount)
            memcpy(expanded + count, Slot_tokens(slot), slotCount * sizeof *expanded);
        count += slotCount;
    }
    return reject((FilterPoolStatus) Compositor_compose(sources, sourceCount, expanded, count, out));
}

// Returns configured slot capacity, or zero for null.
u32 FilterPool_capacity(const FilterPool *pool) {
    return pool ? (*pool).capacity : 0;
}
// Returns the per-recipe filter limit, or zero for null.
u32 FilterPool_maxRecipeFilters(const FilterPool *pool) {
    return pool ? (*pool).maxRecipeFilters : 0;
}
// Returns the number of occupied recipe slots, or zero for null.
u32 FilterPool_live(const FilterPool *pool) {
    return pool ? (*pool).live : 0;
}

// Writes the selected bounded pool projection and reports truncation.
static fn format(const FilterPool *pool, bool structure, char *dest, bytes cap,
                   bool *outTruncated) {
    if (!dest && cap) {
        if (outTruncated)
            *outTruncated = true;
        reject(FILTER_POOL_INVALID);
        return;
    }
    i32 length;
    if (!pool)
        length = snprintf(dest, cap, "nullptr");
    else if (structure)
        length = snprintf(dest, cap,
            "FilterPool{capacity=%u,maxRecipeFilters=%u,live=%u,slots=%p}",
            FilterPool_capacity(pool), FilterPool_maxRecipeFilters(pool), FilterPool_live(pool),
            (any) FilterPool_slots(pool));
    else
        length = snprintf(dest, cap, "FilterPool(live=%u,capacity=%u)",
                          FilterPool_live(pool), FilterPool_capacity(pool));
    if (outTruncated)
        *outTruncated = length < 0 || (bytes) length >= cap;
}
// Formats the pool's concise value summary into caller storage.
fn FilterPool_toString(const FilterPool *pool, char *dest, bytes cap,
                         bool *outTruncated) {
    format(pool, false, dest, cap, outTruncated);
}
// Formats the pool's own fields into caller storage.
fn FilterPool_toStringStruct(const FilterPool *pool, char *dest, bytes cap,
                               bool *outTruncated) {
    format(pool, true, dest, cap, outTruncated);
}
