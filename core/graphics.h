#ifndef GRAPHVEX_CORE_GRAPHICS_H
#define GRAPHVEX_CORE_GRAPHICS_H

/** core/graphics.h -- unified Graphics context, buffer/image ops, and commands. */

#include "core/driver.h"

;;CLASSNAME("Graphics")

typedef struct Graphics Graphics;

Graphics *Graphics_1(VideoPool *pool);
#define Graphics(...) CONSTRUCTOR_DISPATCH(Graphics, __VA_ARGS__)
Graphics *Graphics_createWithDriver(VideoPool *pool, const GraphicsDriver *driver);
fn Graphics_destroy(Graphics *g);

VideoPool *Graphics_pool(const Graphics *g);
const utf8 *Graphics_driverName(const Graphics *g);
u64 Graphics_frameIndex(const Graphics *g);
u32 Graphics_pendingCount(const Graphics *g);
u32 Graphics_bindlessCount(const Graphics *g);

/** Buffer-backed image creation, zero-copy crop, and explicit copy overloads. */
GraphicsImage Graphics_createImage(Graphics *g, VideoArena *arena,
                                   u32 width, u32 height, PixelFormat format);
GraphicsImage Graphics_crop(const GraphicsImage *src, i32 x0, i32 y0, i32 x1, i32 y1);
GraphicsImage Graphics_copy_3(Graphics *g, VideoArena *arena, const GraphicsImage *src);
GraphicsImage Graphics_copy_7(Graphics *g, VideoArena *arena, const GraphicsImage *src,
                              i32 x0, i32 y0, i32 x1, i32 y1);
#define Graphics_copy(...) CONSTRUCTOR_DISPATCH(Graphics_copy, __VA_ARGS__)
bool Graphics_copyTo(Graphics *g, GraphicsImage *dst, i32 dstX, i32 dstY,
                     const GraphicsImage *src);

/** PaintComponent-style target & scissor state. */
fn Graphics_setTarget(Graphics *g, GraphicsImage *target);
GraphicsImage *Graphics_target(const Graphics *g);
fn Graphics_setScissor(Graphics *g, i32 x0, i32 y0, i32 x1, i32 y1);
fn Graphics_resetScissor(Graphics *g);
GraphicsRect Graphics_scissor(const Graphics *g);

/** Shader registration (bindless, backend-agnostic handle). */
u32 Graphics_registerShader(Graphics *g, const utf8 *name, u32 stage);
u32 Graphics_registerCompute(Graphics *g, const utf8 *path, const utf8 *entryName);
u32 Graphics_registerRender(Graphics *g, const utf8 *path, const utf8 *entryPair);

/** Unified command recording and ergonomic command builders. */
bool Graphics_command(Graphics *g, const GraphicsCommand *cmd);
bool Graphics_clear(Graphics *g, u32 colorRGBA);
bool Graphics_dispatch(Graphics *g, u32 shaderId, u32 gridX, u32 gridY, u32 gridZ,
                       vpointer uniformAddr, bytes uniformBytes);
bool Graphics_dispatchWithSource(Graphics *g, u32 shaderId, const GraphicsImage *source,
                                 u32 gridX, u32 gridY, u32 gridZ,
                                 vpointer uniformAddr, bytes uniformBytes);
bool Graphics_draw(Graphics *g, u32 shaderId, vpointer vertexAddr, vpointer indexAddr,
                   u32 count, vpointer uniformAddr, bytes uniformBytes);
bool Graphics_drawWithSource(Graphics *g, u32 shaderId, const GraphicsImage *source,
                             vpointer vertexAddr, vpointer indexAddr,
                             u32 count, vpointer uniformAddr, bytes uniformBytes);

/** Execution and swapchain-free presentation. */
bool Graphics_submit(Graphics *g);
bool Graphics_present(Graphics *g, any nativeSurface, const GraphicsImage *image);

#endif
