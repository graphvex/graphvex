#ifndef GRAPHICS_RENDER_LOOP_H
#define GRAPHICS_RENDER_LOOP_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

#include "util/constructor.h"

// graphvex R3 — graphics/render_loop.h
//
// THE PRESENT-ON-DEMAND LOOP (restored + trimmed from the old graphics_loop).
// A client presents only when it has demand:
//
//     present  <=  painting event  ∨  markDirty(window)  ∨  a newer board generation
//
// No fixed cadence: a resting app presents ZERO times. vexspoke reactive setters
// raise the (coalesced) painting ticket; this loop drains it and presents once,
// so N state changes collapse into ONE frame. The loop owns no GPU resource and
// calls no verb — it is pure scheduling; the present hook is the seam.

typedef struct RenderLoop RenderLoop;

// Frame probe: called each step BEFORE the demand decision. Draws nothing;
// a client whose content changed calls RenderLoop_markDirty here.
typedef fn (*FrameFn)(any userdata, d64 dtSeconds);

// Present hook: build the display list, Graphics_submit it, present. Returns true
// only when a frame was actually presented.
typedef bool (*PresentFn)(any window, d64 dtSeconds, any userdata);

typedef struct Client {
    any window;              // identity key (an R1 window handle)
    FrameFn frameFn;         // demand probe (nullable)
    PresentFn presentFn;     // composite + present (nullable = never presents)
    any userdata;
    _Atomic bool dirty;
    u64 lastContentGen;   // last presented board generation
    u64 contentGen;       // current board generation
    bool hasPresented;
} Client;

RenderLoop *RenderLoop_0();
RenderLoop *RenderLoop_fps(u32 targetFps);   // 0 = pure demand
#define RenderLoop(...) CONSTRUCTOR_DISPATCH(RenderLoop, __VA_ARGS__)
fn RenderLoop_free(RenderLoop *loop);
RenderLoop *RenderLoop_default();             // process singleton

bool RenderLoop_addClient(RenderLoop *loop, const Client *client);
bool RenderLoop_removeClient(RenderLoop *loop, any window);
Client *RenderLoop_findClient(RenderLoop *loop, const_any window);

fn RenderLoop_markDirty(RenderLoop *loop, any window);
fn RenderLoop_setContentGen(RenderLoop *loop, any window, u64 gen);
fn RenderLoop_notify(RenderLoop *loop);         // thread-safe wake

// One demand-driven step. Returns true when at least one client presented.
bool RenderLoop_step(RenderLoop *loop);

#endif // GRAPHICS_RENDER_LOOP_H
