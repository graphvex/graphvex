#ifndef GRAPHVEX_CORE_DRIVER_H
#define GRAPHVEX_CORE_DRIVER_H

/**
 * core/driver.h -- vendor-free backend driver seam implemented by metal/,
 * vulkan/, direct/, and wgpu/. Callers never touch vendor handles.
 */

#include "core/command.h"

;;CLASSNAME("GraphicsDriver")

typedef struct GraphicsDriver {
    string name;
    any context;
    u32 (*registerShader)(any context, string path, string entryName, u32 stage);
    bool (*submitCommands)(any context, const VideoPool *pool,
                           const GraphicsCommand *cmds, u32 count);
    bool (*presentImage)(any context, const VideoPool *pool,
                         any nativeSurface, const GraphicsImage *image);
    fn (*destroyContext)(any context);
} GraphicsDriver;

#endif
