#ifndef CFX_RENDERER3D_H
#define CFX_RENDERER3D_H

#include <stdint.h>
#include <stddef.h>
#include "common.h"
#include "defines.h"

#define CFX_RENDERER3D_STORAGE_WORDS 8

#define CFX_TEXTURE_TILE_SIZE 32
#define CFX_TEXTURE_TILE_COUNT 7
#define CFX_TEXTURE_TILE_PITCH_BYTES CFX_TEXTURE_TILE_SIZE
#define CFX_TEXTURE_TILE_STRIDE_BYTES (CFX_TEXTURE_TILE_SIZE * CFX_TEXTURE_TILE_SIZE)
#define CFX_TEXTURE_ATLAS_WIDTH CFX_TEXTURE_TILE_SIZE
#define CFX_TEXTURE_ATLAS_HEIGHT (CFX_TEXTURE_TILE_SIZE * CFX_TEXTURE_TILE_COUNT)

typedef struct CfxRenderer3D {
    uintptr_t opaque[CFX_RENDERER3D_STORAGE_WORDS];
} CfxRenderer3D;

typedef struct {
    void *framebuffer;
    DEFAULT_INT width;
    DEFAULT_INT height;
} CfxRenderer3DConfig;

void cfx_renderer3d_init(CfxRenderer3D *renderer, const CfxRenderer3DConfig *config);
void cfx_renderer3d_set_framebuffer(CfxRenderer3D *renderer, void *framebuffer);
void cfx_renderer3d_set_texture_atlas(CfxRenderer3D *renderer, const void *atlas, DEFAULT_INT tile_pitch_bytes, DEFAULT_INT tile_stride_bytes);
void cfx_renderer3d_draw_face_list(CfxRenderer3D *renderer, FaceToDraw *faces, DEFAULT_INT face_count);
void cfx_renderer3d_draw_quad(CfxRenderer3D *renderer, const Point2D *p0, const Point2D *p1, const Point2D *p2, const Point2D *p3, DEFAULT_INT tetromino_type);
void cfx_renderer3d_draw_quad_offscreen_direct(CfxRenderer3D *renderer, const Point2D *p0, const Point2D *p1, const Point2D *p2, const Point2D *p3, DEFAULT_INT tetromino_type);
uint32_t cfx_renderer3d_lut_size_bytes(void);

#endif
