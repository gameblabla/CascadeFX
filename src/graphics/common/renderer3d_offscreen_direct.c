#include "renderer3d.h"
#include "renderer3d_port.h"
#include <stddef.h>

#ifndef CFX_RENDERER_DIRECT_KRAM
#define CFX_RENDERER_DIRECT_KRAM 0
#endif

#if CFX_RENDERER_DIRECT_KRAM
extern uint8_t *cfx_game_framebuffer(void);
#endif

#define CFX_TEX_SIZE CFX_TEXTURE_TILE_SIZE
#define CFX_TEX_MASK (CFX_TEX_SIZE - 1)
#define CFX_FIXED_POINT_SHIFT ((CFX_TEX_SIZE == 32) ? 3 : 4)
#define CFX_GEOM_FIXED_SHIFT 8
#define CFX_EDGE_UV_SHIFT 16

typedef struct {
    uint8_t *framebuffer;
    const uint8_t *texture_atlas;
    DEFAULT_INT width;
    DEFAULT_INT height;
    DEFAULT_INT tile_pitch_bytes;
    DEFAULT_INT tile_stride_bytes;
} CfxRenderer3DState;

typedef struct {
    int16_t x, y;
    uint16_t u, v;
} CfxVertexIn;

typedef struct {
    const CfxVertexIn *vertices[3];
    int16_t section;
    int16_t section_height;
    int32_t x;
    int32_t delta_x;
    int32_t u;
    int32_t v;
    int32_t delta_u;
    int32_t delta_v;
} CfxLeftEdge;

typedef struct {
    const CfxVertexIn *vertices[3];
    int16_t section;
    int16_t section_height;
    int32_t x;
    int32_t delta_x;
} CfxRightEdge;

static inline CfxRenderer3DState *cfx_state_direct(CfxRenderer3D *renderer)
{
    return (CfxRenderer3DState *)(void *)renderer->opaque;
}

static inline int32_t cfx_div_toward_zero_direct(int32_t n, int16_t d) { return n / d; }
static inline int32_t cfx_int_to_fixed_direct(int32_t x) { return x << CFX_GEOM_FIXED_SHIFT; }
static inline uint16_t cfx_pack_tex_state_direct(uint8_t u, uint8_t v) { return (uint16_t)(((uint16_t)v << 8) | (uint16_t)u); }

static inline uint16_t cfx_advance_tex_state_n_direct(uint16_t state, int8_t du, int8_t dv, uint16_t count)
{
    uint8_t u = (uint8_t)((uint8_t)state + (uint8_t)((int16_t)du * (int16_t)count));
    uint8_t v = (uint8_t)((uint8_t)(state >> 8) + (uint8_t)((int16_t)dv * (int16_t)count));
    return cfx_pack_tex_state_direct(u, v);
}

static inline void cfx_put_even_pixel_word_direct(uint16_t *word, uint8_t color)
{
    *word = (uint16_t)((*word & 0x00ffu) | ((uint16_t)color << 8));
}

static inline void cfx_put_odd_pixel_word_direct(uint16_t *word, uint8_t color)
{
    *word = (uint16_t)((*word & 0xff00u) | (uint16_t)color);
}

static inline uint16_t cfx_pack_pixel_pair_direct(uint8_t left, uint8_t right)
{
    return (uint16_t)(((uint16_t)left << 8) | (uint16_t)right);
}

static inline uint8_t cfx_fetch_direct_texel_direct(const CfxRenderer3DState *renderer, const uint8_t *tile, uint8_t u, uint8_t v)
{
    return tile[(((uint16_t)(v >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK) *
                 (uint16_t)renderer->tile_pitch_bytes) +
                ((uint16_t)(u >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK)];
}

static inline uint8_t cfx_fetch_direct_row_texel_direct(const uint8_t *row, uint8_t u)
{
    return row[(uint16_t)(u >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK];
}

static void cfx_draw_span_direct_tile_direct(const CfxRenderer3DState *renderer, const uint8_t *tile,
                                             int16_t y, int16_t xs, int16_t span,
                                             uint16_t tex_state, int8_t step_u, int8_t step_v)
{
    if (span <= 0 || y < 0 || y >= renderer->height) return;

    if (xs < 0) {
        int16_t skip = (int16_t)-xs;
        if (skip >= span) return;
        tex_state = cfx_advance_tex_state_n_direct(tex_state, step_u, step_v, (uint16_t)skip);
        span = (int16_t)(span - skip);
        xs = 0;
    }
    if (xs >= renderer->width) return;
    if ((int32_t)xs + span > renderer->width) span = (int16_t)(renderer->width - xs);
    if (span <= 0) return;

    uint8_t u = (uint8_t)tex_state;
    uint8_t v = (uint8_t)(tex_state >> 8);
    uint16_t *dst = (uint16_t *)renderer->framebuffer + ((int32_t)y * (renderer->width >> 1)) + (xs >> 1);

    if (step_v == 0) {
        const uint8_t *row = tile + (((uint16_t)(v >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK) *
                                     (uint16_t)renderer->tile_pitch_bytes);
        if (xs & 1) {
            cfx_put_odd_pixel_word_direct(dst, cfx_fetch_direct_row_texel_direct(row, u));
            u = (uint8_t)(u + (uint8_t)step_u);
            ++dst;
            --span;
        }
        while (span >= 4) {
            const uint8_t c0 = cfx_fetch_direct_row_texel_direct(row, u); u = (uint8_t)(u + (uint8_t)step_u);
            const uint8_t c1 = cfx_fetch_direct_row_texel_direct(row, u); u = (uint8_t)(u + (uint8_t)step_u);
            const uint8_t c2 = cfx_fetch_direct_row_texel_direct(row, u); u = (uint8_t)(u + (uint8_t)step_u);
            const uint8_t c3 = cfx_fetch_direct_row_texel_direct(row, u); u = (uint8_t)(u + (uint8_t)step_u);
            dst[0] = cfx_pack_pixel_pair_direct(c0, c1);
            dst[1] = cfx_pack_pixel_pair_direct(c2, c3);
            dst += 2;
            span = (int16_t)(span - 4);
        }
        if (span >= 2) {
            const uint8_t c0 = cfx_fetch_direct_row_texel_direct(row, u); u = (uint8_t)(u + (uint8_t)step_u);
            const uint8_t c1 = cfx_fetch_direct_row_texel_direct(row, u); u = (uint8_t)(u + (uint8_t)step_u);
            *dst++ = cfx_pack_pixel_pair_direct(c0, c1);
            span = (int16_t)(span - 2);
        }
        if (span > 0) cfx_put_even_pixel_word_direct(dst, cfx_fetch_direct_row_texel_direct(row, u));
        return;
    }

    if (xs & 1) {
        cfx_put_odd_pixel_word_direct(dst, cfx_fetch_direct_texel_direct(renderer, tile, u, v));
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        ++dst;
        --span;
    }
    while (span >= 2) {
        uint8_t c0 = cfx_fetch_direct_texel_direct(renderer, tile, u, v);
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        uint8_t c1 = cfx_fetch_direct_texel_direct(renderer, tile, u, v);
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        *dst++ = cfx_pack_pixel_pair_direct(c0, c1);
        span = (int16_t)(span - 2);
    }
    if (span > 0) cfx_put_even_pixel_word_direct(dst, cfx_fetch_direct_texel_direct(renderer, tile, u, v));
}

static inline const CfxVertexIn *cfx_find_rect_corner_common_direct(const CfxVertexIn *v0, const CfxVertexIn *v1,
                                                                    const CfxVertexIn *v2, const CfxVertexIn *v3,
                                                                    int16_t x, int16_t y)
{
    if (v0->x == x && v0->y == y) return v0;
    if (v1->x == x && v1->y == y) return v1;
    if (v2->x == x && v2->y == y) return v2;
    if (v3->x == x && v3->y == y) return v3;
    return NULL;
}

static uint8_t cfx_get_axis_rect_corners_direct(const CfxVertexIn *v0, const CfxVertexIn *v1,
                                                const CfxVertexIn *v2, const CfxVertexIn *v3,
                                                const CfxVertexIn **tl, const CfxVertexIn **tr,
                                                const CfxVertexIn **bl, int16_t *left, int16_t *right,
                                                int16_t *top, int16_t *bottom)
{
    const CfxVertexIn *vs[4] = { v0, v1, v2, v3 };
    *left = v0->x; *right = v0->x; *top = v0->y; *bottom = v0->y;
    for (uint16_t i = 1; i < 4; ++i) {
        if (vs[i]->x < *left) *left = vs[i]->x;
        if (vs[i]->x > *right) *right = vs[i]->x;
        if (vs[i]->y < *top) *top = vs[i]->y;
        if (vs[i]->y > *bottom) *bottom = vs[i]->y;
    }
    *tl = cfx_find_rect_corner_common_direct(v0, v1, v2, v3, *left, *top);
    *tr = cfx_find_rect_corner_common_direct(v0, v1, v2, v3, *right, *top);
    *bl = cfx_find_rect_corner_common_direct(v0, v1, v2, v3, *left, *bottom);
    return (uint8_t)(*tl && *tr && *bl && *right >= *left && *bottom >= *top);
}

static uint8_t cfx_draw_axis_rect_direct_tile_direct(const CfxRenderer3DState *renderer, const uint8_t *tile,
                                                     const CfxVertexIn *v0, const CfxVertexIn *v1,
                                                     const CfxVertexIn *v2, const CfxVertexIn *v3)
{
    const CfxVertexIn *tl;
    const CfxVertexIn *tr;
    const CfxVertexIn *bl;
    int16_t left, right, top, bottom;
    if (!cfx_get_axis_rect_corners_direct(v0, v1, v2, v3, &tl, &tr, &bl, &left, &right, &top, &bottom)) return 0;

    int16_t width = (int16_t)(right - left + 1);
    int16_t height = (int16_t)(bottom - top + 1);
    if (width <= 0 || height <= 0) return 1;

    int16_t span_step_u = 0, span_step_v = 0, row_step_u = 0, row_step_v = 0;
    if (width > 1) {
        span_step_u = (int16_t)cfx_div_toward_zero_direct((int16_t)(tr->u - tl->u), (int16_t)(width - 1));
        span_step_v = (int16_t)cfx_div_toward_zero_direct((int16_t)(tr->v - tl->v), (int16_t)(width - 1));
    }
    if (height > 1) {
        row_step_u = (int16_t)cfx_div_toward_zero_direct((int16_t)(bl->u - tl->u), (int16_t)(height - 1));
        row_step_v = (int16_t)cfx_div_toward_zero_direct((int16_t)(bl->v - tl->v), (int16_t)(height - 1));
    }

    int16_t row_u = (int16_t)tl->u;
    int16_t row_v = (int16_t)tl->v;
    for (int16_t y = top; y <= bottom; ++y) {
        cfx_draw_span_direct_tile_direct(renderer, tile, y, left, width,
            cfx_pack_tex_state_direct((uint8_t)row_u, (uint8_t)row_v),
            (int8_t)span_step_u, (int8_t)span_step_v);
        row_u = (int16_t)(row_u + row_step_u);
        row_v = (int16_t)(row_v + row_step_v);
    }
    return 1;
}

static int cfx_left_section_direct(CfxLeftEdge *edge)
{
    const CfxVertexIn *v1 = edge->vertices[edge->section];
    const CfxVertexIn *v2 = edge->vertices[edge->section - 1];
    int16_t height = (int16_t)(v2->y - v1->y);
    if (height == 0) return 0;
    edge->section_height = height;
    edge->delta_x = cfx_div_toward_zero_direct(cfx_int_to_fixed_direct((int32_t)v2->x - (int32_t)v1->x), height);
    edge->x = cfx_int_to_fixed_direct(v1->x);
    edge->u = (int32_t)v1->u << CFX_EDGE_UV_SHIFT;
    edge->v = (int32_t)v1->v << CFX_EDGE_UV_SHIFT;
    edge->delta_u = cfx_div_toward_zero_direct(((int32_t)v2->u - (int32_t)v1->u) << CFX_EDGE_UV_SHIFT, height);
    edge->delta_v = cfx_div_toward_zero_direct(((int32_t)v2->v - (int32_t)v1->v) << CFX_EDGE_UV_SHIFT, height);
    return height;
}

static int cfx_right_section_direct(CfxRightEdge *edge)
{
    const CfxVertexIn *v1 = edge->vertices[edge->section];
    const CfxVertexIn *v2 = edge->vertices[edge->section - 1];
    int16_t height = (int16_t)(v2->y - v1->y);
    if (height == 0) return 0;
    edge->section_height = height;
    edge->delta_x = cfx_div_toward_zero_direct(cfx_int_to_fixed_direct((int32_t)v2->x - (int32_t)v1->x), height);
    edge->x = cfx_int_to_fixed_direct(v1->x);
    return height;
}

static void cfx_draw_textured_triangle_direct_tile_direct(const CfxRenderer3DState *renderer,
                                                          const uint8_t *direct_tile,
                                                          CfxVertexIn p1, CfxVertexIn p2, CfxVertexIn p3)
{
    if (p1.y > p2.y) { CfxVertexIn t = p1; p1 = p2; p2 = t; }
    if (p1.y > p3.y) { CfxVertexIn t = p1; p1 = p3; p3 = t; }
    if (p2.y > p3.y) { CfxVertexIn t = p2; p2 = p3; p3 = t; }

    int16_t y1 = p1.y;
    int16_t y2 = p2.y;
    int16_t y3 = p3.y;
    int16_t total_height = (int16_t)(y3 - y1);
    if (total_height == 0) return;

    int16_t temp = (int16_t)cfx_div_toward_zero_direct((y2 - y1) << CFX_GEOM_FIXED_SHIFT, total_height);
    int32_t longest = (int32_t)temp * ((int32_t)p3.x - (int32_t)p1.x) + (((int32_t)p1.x - (int32_t)p2.x) << CFX_GEOM_FIXED_SHIFT);
    if (longest == 0) return;

    CfxLeftEdge left;
    CfxRightEdge right;
    if (longest < 0) {
        right.vertices[0] = &p3; right.vertices[1] = &p2; right.vertices[2] = &p1;
        right.section = 2;
        left.vertices[0] = &p3; left.vertices[1] = &p1; left.vertices[2] = &p1;
        left.section = 1;
        if (cfx_left_section_direct(&left) <= 0) return;
        if (cfx_right_section_direct(&right) <= 0) {
            --right.section;
            if (cfx_right_section_direct(&right) <= 0) return;
        }
    } else {
        left.vertices[0] = &p3; left.vertices[1] = &p2; left.vertices[2] = &p1;
        left.section = 2;
        right.vertices[0] = &p3; right.vertices[1] = &p1; right.vertices[2] = &p1;
        right.section = 1;
        if (cfx_right_section_direct(&right) <= 0) return;
        if (cfx_left_section_direct(&left) <= 0) {
            --left.section;
            if (cfx_left_section_direct(&left) <= 0) return;
        }
    }

    int32_t num_u = (int32_t)temp * ((int32_t)p3.u - (int32_t)p1.u) + cfx_int_to_fixed_direct((int32_t)p1.u - (int32_t)p2.u);
    int32_t num_v = (int32_t)temp * ((int32_t)p3.v - (int32_t)p1.v) + cfx_int_to_fixed_direct((int32_t)p1.v - (int32_t)p2.v);
    int8_t span_step_u = (int8_t)(int16_t)(num_u / longest);
    int8_t span_step_v = (int8_t)(int16_t)(num_v / longest);

    for (;;) {
        int16_t xs = (int16_t)(left.x >> CFX_GEOM_FIXED_SHIFT);
        int16_t xe = (int16_t)(right.x >> CFX_GEOM_FIXED_SHIFT);
        int16_t span = (int16_t)(xe - xs + 1);
        uint16_t tex_state = cfx_pack_tex_state_direct((uint8_t)(left.u >> CFX_EDGE_UV_SHIFT),
                                                       (uint8_t)(left.v >> CFX_EDGE_UV_SHIFT));
        cfx_draw_span_direct_tile_direct(renderer, direct_tile, y1, xs, span,
                                         tex_state, span_step_u, span_step_v);
        ++y1;
        if (--left.section_height <= 0) {
            if (--left.section <= 0) return;
            if (cfx_left_section_direct(&left) <= 0) return;
        } else {
            left.x += left.delta_x;
            left.u += left.delta_u;
            left.v += left.delta_v;
        }
        if (--right.section_height <= 0) {
            if (--right.section <= 0) return;
            if (cfx_right_section_direct(&right) <= 0) return;
        } else {
            right.x += right.delta_x;
        }
    }
}

static inline uint16_t cfx_q8_to_q44_direct(DEFAULT_INT q8)
{
    if (q8 <= 0) return 0;
    if (q8 >= (31 << 8)) return 255;
    int32_t scaled = ((int32_t)q8 * 255) / (31 << 8);
    if (scaled < 0) scaled = 0;
    if (scaled > 255) scaled = 255;
    return (uint16_t)scaled;
}

static inline CfxVertexIn cfx_make_vertex_direct(const Point2D *p)
{
    CfxVertexIn v;
    v.x = (int16_t)p->x;
    v.y = (int16_t)p->y;
    v.u = cfx_q8_to_q44_direct(p->u);
    v.v = cfx_q8_to_q44_direct(p->v);
    return v;
}

void cfx_renderer3d_draw_quad_offscreen_direct(CfxRenderer3D *renderer,
                                               const Point2D *p0, const Point2D *p1,
                                               const Point2D *p2, const Point2D *p3,
                                               DEFAULT_INT tetromino_type)
{
    CfxRenderer3DState *state = cfx_state_direct(renderer);
    if (!state->framebuffer || !state->texture_atlas) return;
#if CFX_RENDERER_DIRECT_KRAM
    if (state->framebuffer == cfx_game_framebuffer()) {
        cfx_renderer3d_draw_quad(renderer, p0, p1, p2, p3, tetromino_type);
        return;
    }
#endif
    if (tetromino_type < 0) tetromino_type = 0;

    const uint8_t *tile = state->texture_atlas + ((int32_t)tetromino_type * state->tile_stride_bytes);
    CfxVertexIn v0 = cfx_make_vertex_direct(p0);
    CfxVertexIn v1 = cfx_make_vertex_direct(p1);
    CfxVertexIn v2 = cfx_make_vertex_direct(p2);
    CfxVertexIn v3 = cfx_make_vertex_direct(p3);

    if (cfx_draw_axis_rect_direct_tile_direct(state, tile, &v0, &v1, &v2, &v3)) return;
    cfx_draw_textured_triangle_direct_tile_direct(state, tile, v0, v1, v2);
    cfx_draw_textured_triangle_direct_tile_direct(state, tile, v0, v2, v3);
}
