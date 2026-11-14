#include "graphics/render_loop.h"
#include "util/annotation.h"

#include <stdlib.h>
#include <time.h>

;;DEFINITION
/** RenderLoop schedules registered clients and invokes their borrowed callbacks
 * only when dirty state or a newer content generation creates presentation
 * demand. It owns no GPU resources; the caller owns client handles and must
 * serialize registry mutation against stepping. */
;;OVERVIEW
/** ========================================================================
 * CLASS: RenderLoop (graphics/render_loop.c)
 * STRUCT FIELDS (Mirroring graphics/render_loop.h)
 * Client *clients               owned growable client registry
 * int count                     initialized registry entries
 * int cap                       allocated registry capacity
 * u32 targetFps             optional target cadence; zero means uncapped
 * _Atomic bool wake              coalesced cross-thread wake request
 * d64 lastSeconds             previous monotonic step time
 * FUNCTION REGISTRY
 * Constructors: RenderLoop *RenderLoop_0(),
 *   RenderLoop *RenderLoop_fps(u32 targetFps),
 *   RenderLoop *RenderLoop_default()
 * Lifecycle: fn RenderLoop_free(RenderLoop *loop)
 * Registry: bool RenderLoop_addClient(RenderLoop *loop,const Client *client)
 *   bool RenderLoop_removeClient(RenderLoop *loop,any window)
 *   Client *RenderLoop_findClient(RenderLoop *loop,const_any window)
 * Demand: fn RenderLoop_markDirty(RenderLoop *loop,any window)
 *   fn RenderLoop_setContentGen(RenderLoop *loop,any window,u64 gen)
 *   fn RenderLoop_notify(RenderLoop *loop)
 *   bool RenderLoop_step(RenderLoop *loop)
 * Private helper: now_seconds reads CLOCK_MONOTONIC for elapsed-time callbacks.
 * Failure, ownership and threading contract:
 * The loop owns the copied client array, but window/userdata and callbacks are
 * borrowed. notify is the atomic wake seam; registry changes and step require
 * external serialization. A failed present leaves dirty/generation demand live.
 * ======================================================================== */

struct RenderLoop {
    Client *clients;
    int count;
    int cap;
    u32 targetFps;   // 0 = pure demand (no cap)
    _Atomic bool wake;
    d64 lastSeconds;
};

// Reads monotonic wall time in seconds for elapsed-frame scheduling.
static d64 now_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (d64)ts.tv_sec + (d64)ts.tv_nsec / 1e9;
}

// Creates a demand-driven loop without an FPS cap.
RenderLoop *RenderLoop_0() { return RenderLoop_fps(0); }

// Creates a loop with the requested target rate; zero leaves presentation uncapped.
RenderLoop *RenderLoop_fps(u32 targetFps) {
    RenderLoop *l = calloc(1, sizeof *l);
    if (l) {
        (*l).targetFps = targetFps;
        (*l).lastSeconds = now_seconds();
    }
    return l;
}

// Frees the loop and its client array; null is ignored.
fn RenderLoop_free(RenderLoop *loop) {
    if (!loop) return;
    free((*loop).clients);
    free(loop);
}

static RenderLoop *s_default = nullptr;
// Returns the lazily allocated process-default demand-driven loop.
RenderLoop *RenderLoop_default() {
    if (!s_default) s_default = RenderLoop_0();
    return s_default;
}

// Adds or replaces a client keyed by its window pointer; allocation failure returns false.
bool RenderLoop_addClient(RenderLoop *loop, const Client *client) {
    if (!loop || !client) return false;
    for (int i = 0; i < (*loop).count; i++) {
        if ((*loop).clients[i].window == (*client).window) {
            (*loop).clients[i] = *client;
            return true;
        }
    }
    if ((*loop).count == (*loop).cap) {
        (*loop).cap = (*loop).cap ? (*loop).cap * 2 : 8;
        Client *grown = realloc((*loop).clients, (bytes)((*loop).cap) * sizeof *grown);
        if (!grown) { (*loop).cap = 0; (*loop).count = 0; return false; }
        (*loop).clients = grown;
    }
    (*loop).clients[(*loop).count++] = *client;
    return true;
}

// Removes the client matching window by swapping in the final array entry.
bool RenderLoop_removeClient(RenderLoop *loop, any window) {
    if (!loop) return false;
    for (int i = 0; i < (*loop).count; i++) {
        if ((*loop).clients[i].window == window) {
            (*loop).clients[i] = (*loop).clients[(*loop).count - 1];
            (*loop).count--;
            return true;
        }
    }
    return false;
}

// Finds a client by window and returns a borrowed pointer into the loop's array.
Client *RenderLoop_findClient(RenderLoop *loop, const_any window) {
    if (!loop) return nullptr;
    for (int i = 0; i < (*loop).count; i++)
        if ((*loop).clients[i].window == window) return &(*loop).clients[i];
    return nullptr;
}

// Marks the matching client's content dirty, if it is registered.
fn RenderLoop_markDirty(RenderLoop *loop, any window) {
    Client *c = RenderLoop_findClient(loop, window);
    if (c) (*c).dirty = true;
}

// Updates the matching client's content generation to trigger demand on the next step.
fn RenderLoop_setContentGen(RenderLoop *loop, any window, u64 gen) {
    Client *c = RenderLoop_findClient(loop, window);
    if (c) (*c).contentGen = gen;
}

// Sets the loop's atomic wake flag when a loop is supplied.
fn RenderLoop_notify(RenderLoop *loop) {
    if (loop) (*loop).wake = true;
}

// Advances frame callbacks and presents clients with dirty or newly generated content.
bool RenderLoop_step(RenderLoop *loop) {
    if (!loop) return false;
    d64 t = now_seconds();
    d64 dt = t - (*loop).lastSeconds;
    (*loop).lastSeconds = t;

    bool presented = false;
    for (int i = 0; i < (*loop).count; i++) {
        Client *c = &(*loop).clients[i];
        if ((*c).frameFn) (*c).frameFn((*c).userdata, dt);
        bool demand = (bool)((*c).dirty) || ((*c).contentGen != (*c).lastContentGen);
        if (!demand) continue;
        bool ok = (*c).presentFn ? (*c).presentFn((*c).window, dt, (*c).userdata) : false;
        if (ok) {
            (*c).hasPresented = true;
            (*c).lastContentGen = (*c).contentGen;
            (*c).dirty = false;
            presented = true;
        }
    }
    (*loop).wake = false;
    return presented;
}
