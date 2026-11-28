#ifndef NIO_GRAPHVEX_MEMORY_H
#define NIO_GRAPHVEX_MEMORY_H

#include "vex.h"
#include "nio/mem.h"
#include "sandcastles/nio/nio.h"

MemoryArena *Graphvex_arena();
MemoryArena *Graphvex_geomArena();
MemoryArena *Graphvex_bufferArena();
MemoryArena *Graphvex_shaderArena();
SecureMemoryPool *Graphvex_securePool();
SecureMemoryArena *Graphvex_secureArena();
fn Graphvex_arenaShutdown();

#endif
