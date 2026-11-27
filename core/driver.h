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
    bool (*submitToSurface)(any context, const VideoPool *pool,
                            any nativeSurface,
                            u32 viewportW, u32 viewportH,
                            u32 surfaceW, u32 surfaceH,
                            const GraphicsCommand *cmds, u32 count);
    bool (*presentImage)(any context, const VideoPool *pool,
                         any nativeSurface, const GraphicsImage *image);
    fn (*setSyncPresent)(any context, bool sync);
    fn (*setSurfacePurgeable)(any context, bool purgeable);
    fn (*destroyContext)(any context);
} GraphicsDriver;

#endif
