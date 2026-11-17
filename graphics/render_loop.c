#include "vex.h"
#include "graphics/render_loop.h"

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
 * i32 count                     initialized registry entries
 * i32 cap                       allocated registry capacity
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
    i32 count;
    i32 cap;
    u32 targetFps;   // 0 = pure demand (no cap)
    _Atomic bool wake;
    d64 lastSeconds;
};

;;GETTER
static Client *RenderLoop_clients(const RenderLoop *l) { return (*l).clients; }
;;SETTER
static fn RenderLoop_setClients(RenderLoop *l, Client *c) { (*l).clients = c; }
;;GETTER
static i32 RenderLoop_count(const RenderLoop *l) { return (*l).count; }
;;SETTER
static fn RenderLoop_setCount(RenderLoop *l, i32 count) { (*l).count = count; }
;;GETTER
static i32 RenderLoop_cap(const RenderLoop *l) { return (*l).cap; }
;;SETTER
static fn RenderLoop_setCap(RenderLoop *l, i32 cap) { (*l).cap = cap; }
;;GETTER
static u32 RenderLoop_targetFps(const RenderLoop *l) { return (*l).targetFps; }
;;SETTER
static fn RenderLoop_setTargetFps(RenderLoop *l, u32 fps) { (*l).targetFps = fps; }
;;GETTER
static _Atomic bool *RenderLoop_wake(RenderLoop *l) { return &(*l).wake; }
;;GETTER
static d64 RenderLoop_lastSeconds(const RenderLoop *l) { return (*l).lastSeconds; }
;;SETTER
static fn RenderLoop_setLastSeconds(RenderLoop *l, d64 s) { (*l).lastSeconds = s; }

;;GETTER
static any Client_window(const Client *c) { return (*c).window; }
;;SETTER
static fn Client_setWindow(Client *c, any w) { (*c).window = w; }
;;GETTER
static FrameFn Client_frameFn(const Client *c) { return (*c).frameFn; }
;;GETTER
static PresentFn Client_presentFn(const Client *c) { return (*c).presentFn; }
;;GETTER
static any Client_userdata(const Client *c) { return (*c).userdata; }
;;GETTER
static _Atomic bool *Client_dirty(Client *c) { return &(*c).dirty; }
;;GETTER
static u64 Client_lastContentGen(const Client *c) { return (*c).lastContentGen; }
;;SETTER
static fn Client_setLastContentGen(Client *c, u64 gen) { (*c).lastContentGen = gen; }
;;GETTER
static u64 Client_contentGen(const Client *c) { return (*c).contentGen; }
;;SETTER
static fn Client_setContentGenValue(Client *c, u64 gen) { (*c).contentGen = gen; }
;;GETTER
static bool Client_hasPresented(const Client *c) { return (*c).hasPresented; }
;;SETTER
static fn Client_setHasPresented(Client *c, bool p) { (*c).hasPresented = p; }

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
        RenderLoop_setTargetFps(l, targetFps);
        RenderLoop_setLastSeconds(l, now_seconds());
    }
    return l;
}

// Frees the loop and its client array; null is ignored.
fn RenderLoop_free(RenderLoop *loop) {
    if (!loop) return;
    free(RenderLoop_clients(loop));
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
    for (i32 i = 0; i < RenderLoop_count(loop); i++) {
        if (Client_window(&RenderLoop_clients(loop)[i]) == Client_window(client)) {
            RenderLoop_clients(loop)[i] = *client;
            return true;
        }
    }
    if (RenderLoop_count(loop) == RenderLoop_cap(loop)) {
        i32 cap = RenderLoop_cap(loop) ? RenderLoop_cap(loop) * 2 : 8;
        Client *grown = realloc(RenderLoop_clients(loop), (bytes)cap * sizeof *grown);
        if (!grown) { RenderLoop_setCap(loop, 0); RenderLoop_setCount(loop, 0); return false; }
        RenderLoop_setClients(loop, grown);
        RenderLoop_setCap(loop, cap);
    }
    RenderLoop_clients(loop)[RenderLoop_count(loop)] = *client;
    RenderLoop_setCount(loop, RenderLoop_count(loop) + 1);
    return true;
}

// Removes the client matching window by swapping in the final array entry.
bool RenderLoop_removeClient(RenderLoop *loop, any window) {
    if (!loop) return false;
    for (i32 i = 0; i < RenderLoop_count(loop); i++) {
        if (Client_window(&RenderLoop_clients(loop)[i]) == window) {
            RenderLoop_clients(loop)[i] = RenderLoop_clients(loop)[RenderLoop_count(loop) - 1];
            RenderLoop_setCount(loop, RenderLoop_count(loop) - 1);
            return true;
        }
    }
    return false;
}

// Finds a client by window and returns a borrowed pointer into the loop's array.
Client *RenderLoop_findClient(RenderLoop *loop, const_any window) {
    if (!loop) return nullptr;
    for (i32 i = 0; i < RenderLoop_count(loop); i++)
        if (Client_window(&RenderLoop_clients(loop)[i]) == window) return &RenderLoop_clients(loop)[i];
    return nullptr;
}

// Marks the matching client's content dirty, if it is registered.
fn RenderLoop_markDirty(RenderLoop *loop, any window) {
    Client *c = RenderLoop_findClient(loop, window);
    if (c) atomic_store_explicit(Client_dirty(c), true, memory_order_relaxed);
}

// Updates the matching client's content generation to trigger demand on the next step.
fn RenderLoop_setContentGen(RenderLoop *loop, any window, u64 gen) {
    Client *c = RenderLoop_findClient(loop, window);
    if (c) Client_setContentGenValue(c, gen);
}

// Sets the loop's atomic wake flag when a loop is supplied.
fn RenderLoop_notify(RenderLoop *loop) {
    if (loop) atomic_store_explicit(RenderLoop_wake(loop), true, memory_order_relaxed);
}

// Advances frame callbacks and presents clients with dirty or newly generated content.
bool RenderLoop_step(RenderLoop *loop) {
    if (!loop) return false;
    d64 t = now_seconds();
    d64 dt = t - RenderLoop_lastSeconds(loop);
    RenderLoop_setLastSeconds(loop, t);

    bool presented = false;
    for (i32 i = 0; i < RenderLoop_count(loop); i++) {
        Client *c = &RenderLoop_clients(loop)[i];
        if (Client_frameFn(c)) Client_frameFn(c)(Client_userdata(c), dt);
        bool demand = (bool)atomic_load_explicit(Client_dirty(c), memory_order_relaxed) || (Client_contentGen(c) != Client_lastContentGen(c));
        if (!demand) continue;
        bool ok = Client_presentFn(c) ? Client_presentFn(c)(Client_window(c), dt, Client_userdata(c)) : false;
        if (ok) {
            Client_setHasPresented(c, true);
            Client_setLastContentGen(c, Client_contentGen(c));
            atomic_store_explicit(Client_dirty(c), false, memory_order_relaxed);
            presented = true;
        }
    }
    atomic_store_explicit(RenderLoop_wake(loop), false, memory_order_relaxed);
    return presented;
}
