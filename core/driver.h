#ifndef GRAPHVEX_CORE_DRIVER_H
#define GRAPHVEX_CORE_DRIVER_H

/**
 * core/driver.h -- vendor-free backend driver seam implemented by metal/,
 * vulkan/, direct/, and wgpu/. Callers never touch vendor handles.
 */

#include "core/command.h"

;;CLASSNAME("GraphicsDriver")

/**
 * Backend driver vtable for Graphics.
 *
 * string name              - backend name ("metal", "software-uma", etc.)
 * any context              - opaque backend context
 * fn registerShader        - loads/compiles compute or render pipeline
 * fn submitCommands        - executes queued commands over ImagePool slabs
 * fn submitToSurface       - executes queued commands directly to OS surface
 * fn presentImage          - presents an Image (Graphics or Backing) to OS surface
 * fn setSyncPresent        - toggles transactional synchronous present
 * fn setSurfacePurgeable   - toggles OS surface volatile purgeability
 * fn importBacking         - wraps external OS hardware backing into driver slot
 * fn allocBacking          - allocates OS-owned hardware backing in driver slot
 * fn releaseBacking        - releases driver backing slot and handle
 * fn setBackingPurgeable   - toggles volatile purgeability on a backing slot
 * fn isBackingInUse        - queries whether OS/display holds a backing slot
 * fn attachPool            - registers an auxiliary domain pool (e.g. CompositorPool)
 * fn detachPool            - unregisters an auxiliary domain pool
 * fn destroyContext        - tears down backend context
 */
typedef struct GraphicsDriver {
    string name;
    any context;
    u32 (*registerShader)(any context, string path, string entryName, u32 stage);
    bool (*submitCommands)(any context, const ImagePool *pool,
                           const GraphicsCommand *cmds, u32 count);
    bool (*submitToSurface)(any context, const ImagePool *pool,
                            any nativeSurface,
                            u32 viewportW, u32 viewportH,
                            u32 surfaceW, u32 surfaceH,
                            const GraphicsCommand *cmds, u32 count);
    bool (*presentImage)(any context, const ImagePool *pool,
                         any nativeSurface, const Image *image);
    fn (*setSyncPresent)(any context, bool sync);
    fn (*setSurfacePurgeable)(any context, bool purgeable);
    bool (*importBacking)(any context, any nativeBacking,
                          u32 width, u32 height, PixelFormat format,
                          bool ownsSurface,
                          u32 *outBackingSlot, u32 *outPlaneCount,
                          u32 *outStrideBytes);
    bool (*allocBacking)(any context, u32 width, u32 height, PixelFormat format,
                         any *outNativeBacking, u32 *outBackingSlot,
                         u32 *outPlaneCount, u32 *outStrideBytes);
    fn (*releaseBacking)(any context, u32 backingSlot);
    bool (*setBackingPurgeable)(any context, u32 backingSlot, bool purgeable);
    bool (*isBackingInUse)(any context, u32 backingSlot);
    bool (*attachPool)(any context, const ImagePool *auxPool);
    fn (*detachPool)(any context, const ImagePool *auxPool);
    fn (*destroyContext)(any context);
} GraphicsDriver;

#endif
