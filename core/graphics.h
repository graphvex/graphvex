#ifndef GRAPHVEX_CORE_GRAPHICS_H
#define GRAPHVEX_CORE_GRAPHICS_H

/** core/graphics.h -- unified Graphics context, image ops, and commands. */

#include "core/driver.h"

#define GRAPHICS_MAX_AUX_POOLS 8u

;;CLASSNAME("GraphicsBackend")

typedef enum GraphicsBackend {
    GRAPHICS_AUTO     = 0,
    GRAPHICS_METAL    = 1,
    GRAPHICS_VULKAN   = 2,
    GRAPHICS_D3D12    = 3,
    GRAPHICS_WGPU     = 4,
    GRAPHICS_SOFTWARE = 5,
} GraphicsBackend;

/**
 * Boot-time process-wide backend override (SCREAMING_CASE: call at the top of
 * main() before creating any ImagePool, Graphics, or Frame).
 * @param backend requested GraphicsBackend override
 * @return true if accepted before latch, false if Graphics is already initialized
 */
bool GRAPHICS_OVERRIDE(GraphicsBackend backend);
GraphicsBackend GRAPHICS_BACKEND();
fn Graphics_latchBackend();

;;CLASSNAME("Graphics")

typedef struct Graphics Graphics;

Graphics *Graphics_1(ImagePool *pool);
Graphics *Graphics_2(ImagePool *pool, const GraphicsDriver *driver);
#define Graphics(...) CONSTRUCTOR_DISPATCH(Graphics, __VA_ARGS__)
fn Graphics_destroy(Graphics *g);

ImagePool *Graphics_pool(const Graphics *g);
const ImagePool *Graphics_resolvePool(const Graphics *g, vpointer addr, bytes sizeBytes);
bool Graphics_attachPool(Graphics *g, const ImagePool *auxPool);
fn Graphics_detachPool(Graphics *g, const ImagePool *auxPool);
string Graphics_driverName(const Graphics *g);
u64 Graphics_frameIndex(const Graphics *g);
u32 Graphics_pendingCount(const Graphics *g);
u32 Graphics_bindlessCount(const Graphics *g);

GraphicsImage Graphics_crop(const GraphicsImage *src, i32 x0, i32 y0, i32 x1, i32 y1);
GraphicsImage Graphics_copy_3(Graphics *g, ImageArena *arena, const Image *src);
GraphicsImage Graphics_copy_7(Graphics *g, ImageArena *arena, const GraphicsImage *src,
                              i32 x0, i32 y0, i32 x1, i32 y1);
#define Graphics_copy(...) CONSTRUCTOR_DISPATCH(Graphics_copy, __VA_ARGS__)
bool Graphics_copyTo(Graphics *g, GraphicsImage *dst, i32 dstX, i32 dstY,
                     const Image *src);

fn Graphics_setTarget(Graphics *g, Image *target);
Image *Graphics_target(const Graphics *g);
fn Graphics_setScissor(Graphics *g, i32 x0, i32 y0, i32 x1, i32 y1);
fn Graphics_resetScissor(Graphics *g);
GraphicsRect Graphics_scissor(const Graphics *g);

u32 Graphics_registerShader(Graphics *g, string name, u32 stage);
u32 Graphics_registerCompute(Graphics *g, string path, string entryName);
u32 Graphics_registerRender(Graphics *g, string path, string entryPair);

bool Graphics_command(Graphics *g, const GraphicsCommand *cmd);
bool Graphics_clear(Graphics *g, u32 colorRGBA);
bool Graphics_dispatch(Graphics *g, u32 shaderId, u32 gridX, u32 gridY, u32 gridZ,
                       vpointer uniformAddr, bytes uniformBytes);
bool Graphics_dispatchWithSource(Graphics *g, u32 shaderId, const Image *source,
                                 u32 gridX, u32 gridY, u32 gridZ,
                                 vpointer uniformAddr, bytes uniformBytes);
bool Graphics_draw(Graphics *g, u32 shaderId, vpointer vertexAddr, vpointer indexAddr,
                   u32 count, vpointer uniformAddr, bytes uniformBytes);
bool Graphics_drawWithSource(Graphics *g, u32 shaderId, const Image *source,
                             vpointer vertexAddr, vpointer indexAddr,
                             u32 count, vpointer uniformAddr, bytes uniformBytes);

bool Graphics_submit(Graphics *g);
bool Graphics_supportsDirectSurface(const Graphics *g);
bool Graphics_submitToSurface(Graphics *g, any nativeSurface,
                              u32 viewportW, u32 viewportH,
                              u32 surfaceW, u32 surfaceH, bool sync);
fn Graphics_setSyncPresent(Graphics *g, bool sync);
fn Graphics_setSurfacePurgeable(Graphics *g, bool purgeable);
bool Graphics_present(Graphics *g, any nativeSurface, const Image *image);
bool Graphics_presentSync(Graphics *g, any nativeSurface, const Image *image);

#endif
