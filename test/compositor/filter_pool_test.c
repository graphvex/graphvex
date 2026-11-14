// Owner for compositor/filter_pool.{h,c}: immutable CPU complex-filter records.
// Surface inventory: constructor, destroy, insert, retain/release, snapshot,
// compose, capacity/maxRecipeFilters/live, bounded value/struct strings.
// Proves copying/order, lifetime/stale rejection, nested/unknown rejection,
// configured limits, failure-unchanged outputs, generation exhaustion, inline
// compose equivalence, and exactly one cold diagnostic per detected rejection.
// Gaps: allocation-fault injection and uint32 refcount saturation (billions of
// calls); no concurrency, cross-pool provenance, GPU or steady-state claims.

#include "vex.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../../compositor/filter_pool.h"

static int failures;
static unsigned expectedDiagnostics;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define ERROR(call, status) do { ++expectedDiagnostics; CHECK((call) == (status)); } while (0)

// Creates a filter pool with the requested slot and per-stack limits.
static FilterPool *pool(u32 capacity, u32 maxFilters) {
    FilterPool *p = nullptr;
    FilterPoolConfig config = {capacity, maxFilters};
    CHECK(FilterPool(config, &p) == FILTER_POOL_OK);
    return p;
}

// Verifies pool allocation, borrowed token lifetime, and release behavior.
static fn lifetime() {
    FilterPool *p = pool(1, 2);
    CHECK(FilterPool_capacity(p) == 1 && FilterPool_maxRecipeFilters(p) == 2);
    CHECK(FilterPool_live(p) == 0);
    FilterToken recipe[] = {Filter_gain(2), Filter_scatterBlur(1)};
    FilterToken token = 99;
    CHECK(FilterPool_insert(p, recipe, 2, &token) == FILTER_POOL_OK);
    CHECK(Filter_id(token) == FILTER_POOL_ID && (u32) token == 0);
    recipe[0] = Filter_gain(7); // mutation cannot alter the stored recipe
    FilterToken snapshot[2] = {99, 88};
    bytes count = 99;
    ERROR(FilterPool_snapshot(p, token, snapshot, 1, &count), FILTER_POOL_LIMIT);
    CHECK(snapshot[0] == 99 && snapshot[1] == 88 && count == 99);
    CHECK(FilterPool_snapshot(p, token, snapshot, 2, &count) == FILTER_POOL_OK);
    CHECK(count == 2 && snapshot[0] == Filter_gain(2) && snapshot[1] == recipe[1]);
    snapshot[0] = Filter_identity();
    CHECK(FilterPool_retain(p, token) == FILTER_POOL_OK);
    ERROR(FilterPool_destroy(p), FILTER_POOL_BUSY);
    FilterToken unchanged = 123;
    ERROR(FilterPool_insert(p, nullptr, 0, &unchanged), FILTER_POOL_LIMIT);
    CHECK(unchanged == 123 && FilterPool_live(p) == 1);
    CHECK(FilterPool_release(p, token) == FILTER_POOL_OK);
    CHECK(FilterPool_snapshot(p, token, snapshot, 2, &count) == FILTER_POOL_OK);
    CHECK(snapshot[0] == Filter_gain(2));
    CHECK(FilterPool_release(p, token) == FILTER_POOL_OK);
    ERROR(FilterPool_release(p, token), FILTER_POOL_STALE);
    ERROR(FilterPool_retain(p, token), FILTER_POOL_STALE);
    ERROR(FilterPool_snapshot(p, token, snapshot, 2, &count), FILTER_POOL_STALE);
    FilterToken replacement;
    CHECK(FilterPool_insert(p, nullptr, 0, &replacement) == FILTER_POOL_OK);
    CHECK(replacement != token);
    CHECK(FilterPool_snapshot(p, replacement, nullptr, 0, &count) == FILTER_POOL_OK);
    CHECK(count == 0);
    CHECK(FilterPool_release(p, replacement) == FILTER_POOL_OK);
    CHECK(FilterPool_destroy(p) == FILTER_POOL_OK);
}

// Checks pool configuration, malformed strings, and diagnostic failures.
static fn invalid_and_strings() {
    FilterPool *p = nullptr;
    ERROR(FilterPool_2((FilterPoolConfig){0, 1}, &p), FILTER_POOL_INVALID);
    ERROR(FilterPool_2((FilterPoolConfig){4097, 1}, &p), FILTER_POOL_LIMIT);
    ERROR(FilterPool_2((FilterPoolConfig){1, 257}, &p), FILTER_POOL_LIMIT);
    ERROR(FilterPool_2((FilterPoolConfig){1, 1}, nullptr), FILTER_POOL_INVALID);
    CHECK(p == nullptr);
    p = pool(2, 2);
    FilterToken out = 99;
    FilterToken invalid = ((u64) FILTER_POOL_ID << 48) | (UINT64_C(1) << 32);
    ERROR(FilterPool_insert(p, &invalid, 1, &out), FILTER_POOL_UNSUPPORTED);
    invalid = UINT64_C(0x7777000000000000);
    ERROR(FilterPool_insert(p, &invalid, 1, &out), FILTER_POOL_UNSUPPORTED);
    invalid = Filter_gain(NAN);
    ERROR(FilterPool_insert(p, &invalid, 1, &out), FILTER_POOL_INVALID);
    invalid = Filter_scatterBlur(17);
    ERROR(FilterPool_insert(p, &invalid, 1, &out), FILTER_POOL_INVALID);
    ERROR(FilterPool_insert(p, nullptr, 1, &out), FILTER_POOL_INVALID);
    ERROR(FilterPool_insert(p, nullptr, 3, &out), FILTER_POOL_INVALID);
    FilterToken three[3] = {0};
    ERROR(FilterPool_insert(p, three, 3, &out), FILTER_POOL_LIMIT);
    ERROR(FilterPool_insert(nullptr, nullptr, 0, &out), FILTER_POOL_INVALID);
    ERROR(FilterPool_insert(p, nullptr, 0, nullptr), FILTER_POOL_INVALID);
    CHECK(out == 99 && FilterPool_live(p) == 0);
    ERROR(FilterPool_retain(p, (u64) FILTER_POOL_ID << 48), FILTER_POOL_INVALID);
    ERROR(FilterPool_retain(p, ((u64) FILTER_POOL_ID << 48) |
          (UINT64_C(1) << 32) | UINT32_MAX), FILTER_POOL_STALE);
    ERROR(FilterPool_release(nullptr, 0), FILTER_POOL_INVALID);
    ERROR(FilterPool_retain(p, Filter_identity()), FILTER_POOL_INVALID);
    CHECK(FilterPool_insert(p, three, 2, &out) == FILTER_POOL_OK);
    bytes count = 44;
    ERROR(FilterPool_snapshot(p, out, nullptr, 2, &count), FILTER_POOL_INVALID);
    ERROR(FilterPool_snapshot(p, out, three, 2, nullptr), FILTER_POOL_INVALID);
    CHECK(count == 44 && FilterPool_live(p) == 1);
    char text[160];
    bool truncated = true;
    FilterPool_toString(p, text, sizeof text, &truncated);
    CHECK(!truncated && strstr(text, "live=1") != nullptr);
    FilterPool_toStringStruct(p, text, sizeof text, &truncated);
    CHECK(!truncated && strstr(text, "capacity=2,maxRecipeFilters=2,live=1,slots=") != nullptr);
    FilterPool_toString(nullptr, text, sizeof text, &truncated);
    CHECK(!truncated && strcmp(text, "nullptr") == 0);
    FilterPool_toStringStruct(nullptr, text, 1, &truncated);
    CHECK(truncated && text[0] == '\0');
    FilterPool_toString(p, nullptr, 0, &truncated);
    CHECK(truncated);
    FilterPool_toString(p, text, sizeof text, nullptr);
    ++expectedDiagnostics;
    FilterPool_toString(p, nullptr, 1, &truncated);
    CHECK(truncated);
    CHECK(FilterPool_capacity(nullptr) == 0 && FilterPool_maxRecipeFilters(nullptr) == 0);
    CHECK(FilterPool_live(nullptr) == 0);
    CHECK(FilterPool_release(p, out) == FILTER_POOL_OK);
    CHECK(FilterPool_destroy(p) == FILTER_POOL_OK);
    CHECK(FilterPool_destroy(nullptr) == FILTER_POOL_OK);
}

// Exercises filter-pool tokens through compositor composition.
static fn composition() {
    FilterPool *p = pool(2, COMPOSITOR_MAX_FILTERS);
    FilterToken recipe[] = {Filter_gain(2), Filter_scatterBlur(1)};
    FilterToken token;
    CHECK(FilterPool_insert(p, recipe, 2, &token) == FILTER_POOL_OK);
    FilterToken stack[] = {Filter_gain(3), token, token};
    FilterToken inlineStack[] = {Filter_gain(3), recipe[0], recipe[1], recipe[0], recipe[1]};
    CompositorSurface *source = nullptr, *actual = nullptr, *expected = nullptr;
    CHECK(CompositorSurface_create((CompositorBounds){-2, 3, 1, 1}, &source) == COMPOSITOR_OK);
    f32 *pixels = CompositorSurface_pixels(source);
    pixels[0] = 0.25f; pixels[3] = 0.5f;
    const CompositorSurface *sources[] = {source};
    CHECK(FilterPool_compose(p, sources, 1, stack, 3, &actual) == FILTER_POOL_OK);
    CHECK(Compositor_compose(sources, 1, inlineStack, 5, &expected) == COMPOSITOR_OK);
    CompositorBounds a = CompositorSurface_bounds(actual), b = CompositorSurface_bounds(expected);
    CHECK(a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height);
    CHECK(a.x == -4 && a.y == 1 && a.width == 5 && a.height == 5);
    CHECK(memcmp(CompositorSurface_constPixels(actual), CompositorSurface_constPixels(expected),
                 (bytes) a.width * a.height * 4 * sizeof(f32)) == 0);
    CHECK(pixels[0] == 0.25f && pixels[3] == 0.5f);
    CompositorSurface *unchanged = actual;
    FilterToken full[COMPOSITOR_MAX_FILTERS] = {0}, fullToken;
    CHECK(FilterPool_insert(p, full, COMPOSITOR_MAX_FILTERS, &fullToken) == FILTER_POOL_OK);
    FilterToken oversized[] = {fullToken, Filter_identity()};
    ERROR(FilterPool_compose(p, sources, 1, oversized, 2, &actual), FILTER_POOL_LIMIT);
    FilterToken unknown = UINT64_C(0x7777000000000000);
    ERROR(FilterPool_compose(p, sources, 1, &unknown, 1, &actual), FILTER_POOL_UNSUPPORTED);
    ERROR(FilterPool_compose(p, nullptr, 1, nullptr, 0, &actual), FILTER_POOL_INVALID);
    ERROR(FilterPool_compose(p, sources, 1, nullptr, 1, &actual), FILTER_POOL_INVALID);
    ERROR(FilterPool_compose(nullptr, sources, 1, nullptr, 0, &actual), FILTER_POOL_INVALID);
    ERROR(FilterPool_compose(p, sources, 1, nullptr, 0, nullptr), FILTER_POOL_INVALID);
    ERROR(FilterPool_compose(p, sources, 257, nullptr, 0, &actual), FILTER_POOL_LIMIT);
    ERROR(FilterPool_compose(p, sources, 1, full, 257, &actual), FILTER_POOL_LIMIT);
    CHECK(FilterPool_release(p, token) == FILTER_POOL_OK);
    ERROR(FilterPool_compose(p, sources, 1, &token, 1, &actual), FILTER_POOL_STALE);
    CHECK(actual == unchanged);
    CHECK(FilterPool_release(p, fullToken) == FILTER_POOL_OK);
    CHECK(FilterPool_destroy(p) == FILTER_POOL_OK);
    CompositorSurface_destroy(actual);
    CompositorSurface_destroy(expected);
    CompositorSurface_destroy(source);
}

// Verifies exhausted generations reject safely without aliasing old handles.
static fn generation_exhaustion() {
    FilterPool *p = pool(1, 1);
    FilterToken first = 0, last = 0;
    for (u32 generation = 1; generation <= UINT16_MAX; ++generation) {
        CHECK(FilterPool_insert(p, nullptr, 0, &last) == FILTER_POOL_OK);
        CHECK((u16) (Filter_payload(last) >> 32) == generation);
        if (generation == 1)
            first = last;
        CHECK(FilterPool_release(p, last) == FILTER_POOL_OK);
    }
    FilterToken unchanged = 99;
    ERROR(FilterPool_insert(p, nullptr, 0, &unchanged), FILTER_POOL_LIMIT);
    ERROR(FilterPool_retain(p, first), FILTER_POOL_STALE);
    ERROR(FilterPool_retain(p, last), FILTER_POOL_STALE);
    CHECK(unchanged == 99 && FilterPool_live(p) == 0);
    CHECK(FilterPool_destroy(p) == FILTER_POOL_OK);
}

// Runs filter-pool lifetime, validation, composition, and exhaustion cases.
int main() {
    FILE *diagnostics = tmpfile();
    if (!diagnostics)
        return 1;
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    if (saved < 0 || dup2(fileno(diagnostics), STDERR_FILENO) < 0)
        return 1;
    lifetime();
    invalid_and_strings();
    composition();
    generation_exhaustion();
    fflush(stderr);
    CHECK(dup2(saved, STDERR_FILENO) >= 0);
    close(saved);
    rewind(diagnostics);
    unsigned observed = 0;
    char line[512];
    while (fgets(line, sizeof line, diagnostics)) {
        CHECK(strstr(line, "[vex] ") && strstr(line, "FilterPool rejected operation: status="));
        ++observed;
    }
    CHECK(observed == expectedDiagnostics);
    fclose(diagnostics);
    if (!failures)
        puts("filter_pool_test: PASS (CPU pool lifetime, expansion, saturation and diagnostics)");
    return failures ? 1 : 0;
}
