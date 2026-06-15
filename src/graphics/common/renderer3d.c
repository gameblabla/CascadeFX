#include "renderer3d.h"
#include "renderer3d_port.h"
#include <stddef.h>

#ifndef CFX_RENDERER_DIRECT_KRAM
#define CFX_RENDERER_DIRECT_KRAM 0
#endif

#if CFX_RENDERER_DIRECT_KRAM
extern uint8_t *cfx_game_framebuffer(void);
extern int cfx_pcfx_current_page_word_offset(void);
extern int cfx_pcfx_current_kram_page_word_offset;
extern void eris_king_set_kram_write(uint32_t addr, int incr);
#endif

#ifndef CFX_RENDERER_DIRECT_ROW_LUT
#define CFX_RENDERER_DIRECT_ROW_LUT 0
#endif

#ifndef CFX_RENDERER_QUAD_SCANLINE
#define CFX_RENDERER_QUAD_SCANLINE 0
#endif

#ifndef CFX_RENDERER_DIRECT_GENERIC_TILE
#define CFX_RENDERER_DIRECT_GENERIC_TILE 0
#endif

#define CFX_TEX_SIZE CFX_TEXTURE_TILE_SIZE
#define CFX_TEX_MASK (CFX_TEX_SIZE - 1)
#define CFX_FIXED_POINT_SHIFT ((CFX_TEX_SIZE == 32) ? 3 : 4)
#define CFX_GEOM_FIXED_SHIFT 8
#define CFX_QUAD_UV_SHIFT 8
#define CFX_EDGE_UV_SHIFT 16
#define CFX_MAX_TEXTURE_TILES 7

typedef struct {
    uint8_t *framebuffer;
    const uint8_t *texture_atlas;
    DEFAULT_INT width;
    DEFAULT_INT height;
    DEFAULT_INT tile_pitch_bytes;
    DEFAULT_INT tile_stride_bytes;
} CfxRenderer3DState;

static inline CfxRenderer3DState *cfx_state(CfxRenderer3D *renderer)
{
    return (CfxRenderer3DState *)(void *)renderer->opaque;
}

#if CFX_RENDERER_DIRECT_KRAM
static inline __attribute__((always_inline)) uint8_t cfx_renderer3d_direct_kram_active(const CfxRenderer3DState *renderer)
{
    return (uint8_t)(renderer->framebuffer == cfx_game_framebuffer());
}

static inline __attribute__((always_inline)) void cfx_pcfx_kram_select_data_register(void)
{
    __asm__ volatile (
        "movea 14, r0, r10\n\t"
        "out.h r10, 0x600[r0]\n\t"
        :
        :
        : "r10", "memory");
}

static inline __attribute__((always_inline)) void cfx_pcfx_kram_seek_frame_word(int32_t frame_word_index)
{
    /* Inline eris_king_set_kram_write(addr, 1) plus select KRAM data register.
       The scanline rasterizer calls this once per emitted row, so avoiding the
       liberis function call matters on V810.  KING register 0x0D receives the
       write address ORed with increment<<18; register 0x0E is the data port. */
    uint32_t addr = (uint32_t)(cfx_pcfx_current_kram_page_word_offset + frame_word_index) | (1u << 18);
    __asm__ volatile (
        "movea 13, r0, r10\n\t"
        "out.h r10, 0x600[r0]\n\t"
        "out.w %[addr], 0x604[r0]\n\t"
        "movea 14, r0, r10\n\t"
        "out.h r10, 0x600[r0]\n\t"
        :
        : [addr] "r" (addr)
        : "r10", "memory");
}

static inline __attribute__((always_inline)) void cfx_pcfx_kram_write_word(uint16_t value)
{
    __asm__ volatile ("out.h %0, 0x604[r0]" : : "r" (value) : "memory");
}
#endif

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

typedef struct {
    int16_t y_start;
    int16_t y_end;
    int32_t x;
    int32_t x_step;
    int32_t u;
    int32_t v;
    int32_t u_step;
    int32_t v_step;
} CfxQuadEdge;

/* Active 16x16 texture lookup table.  The previous renderer used this
   64 KiB table only as a texel-index LUT, then performed a second texture
   fetch per pixel.  The fast 16x16 mapper is intended to make the inner
   span loop one LUT read + one state add per pixel, so this table stores the
   final palette-index texel for the currently selected 16x16 tile.
   The texture palette is remapped below index 128 so V810/SH1 signed byte
   loads can be packed directly without zero-extension in the hot loop. */
static uint8_t tex_lut[1u << 16] __attribute__((aligned(16)));
#if CFX_RENDERER_MULTI_LUT
static uint8_t tex_lut_tiles[CFX_MAX_TEXTURE_TILES][1u << 16] __attribute__((aligned(16)));
static const uint8_t *active_tex_lut = tex_lut_tiles[0];
static const uint8_t *tex_lut_tiles_source_atlas;
static DEFAULT_INT tex_lut_tiles_source_pitch;
static DEFAULT_INT tex_lut_tiles_source_stride;
static uint8_t tex_lut_tiles_ready;
static uint8_t tex_lut_tile_ready[CFX_MAX_TEXTURE_TILES];
#else
static const uint8_t *active_tex_lut = tex_lut;
#endif
static const uint8_t *tex_lut_source_tile;
static DEFAULT_INT tex_lut_source_pitch;
static uint8_t tex_lut_ready;

#if CFX_RENDERER_DIRECT_ROW_LUT
static uint8_t tex_row_lut_tiles[CFX_MAX_TEXTURE_TILES][CFX_TEX_SIZE][256] __attribute__((aligned(16)));
static const uint8_t *tex_row_lut_source_atlas;
static DEFAULT_INT tex_row_lut_source_pitch;
static DEFAULT_INT tex_row_lut_source_stride;
static uint8_t tex_row_lut_ready;
#endif

static inline int32_t cfx_abs_i32(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t cfx_div_toward_zero(int32_t n, int16_t d) { return n / d; }
static inline int32_t cfx_int_to_fixed(int32_t x) { return x << CFX_GEOM_FIXED_SHIFT; }
static inline uint16_t cfx_pack_tex_state(uint8_t u, uint8_t v) { return (uint16_t)(((uint16_t)v << 8) | (uint16_t)u); }
static inline int16_t cfx_pack_tex_step_linear(int8_t du, int8_t dv)
{
    return (int16_t)(((int16_t)dv << 8) + (int16_t)du);
}

static inline uint16_t cfx_advance_tex_state(uint16_t state, int8_t du, int8_t dv)
{
    uint8_t u = (uint8_t)((uint8_t)state + (uint8_t)du);
    uint8_t v = (uint8_t)((uint8_t)(state >> 8) + (uint8_t)dv);
    return cfx_pack_tex_state(u, v);
}

static inline uint16_t cfx_advance_tex_state_n(uint16_t state, int8_t du, int8_t dv, uint16_t count)
{
    uint8_t u = (uint8_t)((uint8_t)state + (uint8_t)((int16_t)du * (int16_t)count));
    uint8_t v = (uint8_t)((uint8_t)(state >> 8) + (uint8_t)((int16_t)dv * (int16_t)count));
    return cfx_pack_tex_state(u, v);
}

static void cfx_fill_lut_from_tile(const CfxRenderer3DState *renderer, const uint8_t *tile, uint8_t *lut)
{
    for (uint16_t v = 0; v < 256; ++v) {
        const uint8_t *src_row = tile + (((uint16_t)(v >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK) *
            (uint16_t)renderer->tile_pitch_bytes);
        uint8_t *dst = lut + ((uint16_t)v << 8);
        for (uint16_t u = 0; u < 256; ++u) {
            dst[u] = src_row[(uint16_t)(u >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK];
        }
    }
}

#if CFX_RENDERER_DIRECT_ROW_LUT
static void cfx_renderer3d_build_direct_row_luts(const CfxRenderer3DState *renderer)
{
    if (tex_row_lut_ready &&
        tex_row_lut_source_atlas == renderer->texture_atlas &&
        tex_row_lut_source_pitch == renderer->tile_pitch_bytes &&
        tex_row_lut_source_stride == renderer->tile_stride_bytes) {
        return;
    }

    for (uint16_t tile_index = 0; tile_index < CFX_MAX_TEXTURE_TILES; ++tile_index) {
        const uint8_t *tile = renderer->texture_atlas + ((int32_t)tile_index * renderer->tile_stride_bytes);
        for (uint16_t row = 0; row < CFX_TEX_SIZE; ++row) {
            const uint8_t *src_row = tile + ((uint16_t)row * (uint16_t)renderer->tile_pitch_bytes);
            uint8_t *dst = tex_row_lut_tiles[tile_index][row];
            for (uint16_t u = 0; u < 256; ++u) {
                dst[u] = src_row[(uint16_t)(u >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK];
            }
        }
    }

    tex_row_lut_source_atlas = renderer->texture_atlas;
    tex_row_lut_source_pitch = renderer->tile_pitch_bytes;
    tex_row_lut_source_stride = renderer->tile_stride_bytes;
    tex_row_lut_ready = 1;
}

static inline const uint8_t *cfx_renderer3d_direct_row_lut(const CfxRenderer3DState *renderer, const uint8_t *tile, uint8_t v)
{
    if (!renderer->texture_atlas || renderer->tile_stride_bytes <= 0) {
        return NULL;
    }

    intptr_t byte_delta = tile - renderer->texture_atlas;
    if (byte_delta < 0 || (byte_delta % renderer->tile_stride_bytes) != 0) {
        return NULL;
    }

    intptr_t tile_index = byte_delta / renderer->tile_stride_bytes;
    if (tile_index < 0 || tile_index >= CFX_MAX_TEXTURE_TILES) {
        return NULL;
    }

    cfx_renderer3d_build_direct_row_luts(renderer);
    return tex_row_lut_tiles[tile_index][(uint16_t)(v >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK];
}
#endif

#if CFX_RENDERER_MULTI_LUT
static void cfx_renderer3d_reset_pcfx_tile_lut_cache(const CfxRenderer3DState *renderer)
{
    if (tex_lut_tiles_ready && tex_lut_tiles_source_atlas == renderer->texture_atlas &&
        tex_lut_tiles_source_pitch == renderer->tile_pitch_bytes &&
        tex_lut_tiles_source_stride == renderer->tile_stride_bytes) {
        return;
    }

    for (uint16_t tile_index = 0; tile_index < CFX_MAX_TEXTURE_TILES; ++tile_index) {
        tex_lut_tile_ready[tile_index] = 0;
    }

    tex_lut_tiles_source_atlas = renderer->texture_atlas;
    tex_lut_tiles_source_pitch = renderer->tile_pitch_bytes;
    tex_lut_tiles_source_stride = renderer->tile_stride_bytes;
    tex_lut_tiles_ready = 1;
}

static void cfx_renderer3d_select_pcfx_tile_lut(const CfxRenderer3DState *renderer, DEFAULT_INT tile_index)
{
    cfx_renderer3d_reset_pcfx_tile_lut_cache(renderer);

    if (tile_index < 0) {
        tile_index = 0;
    }
    if (tile_index >= CFX_MAX_TEXTURE_TILES) {
        tile_index = CFX_MAX_TEXTURE_TILES - 1;
    }

    if (!tex_lut_tile_ready[tile_index]) {
        const uint8_t *tile = renderer->texture_atlas + ((int32_t)tile_index * renderer->tile_stride_bytes);
        cfx_fill_lut_from_tile(renderer, tile, tex_lut_tiles[tile_index]);
        tex_lut_tile_ready[tile_index] = 1;
    }

    active_tex_lut = tex_lut_tiles[tile_index];
}

static void cfx_renderer3d_prebuild_pcfx_tile_luts(const CfxRenderer3DState *renderer)
{
    cfx_renderer3d_reset_pcfx_tile_lut_cache(renderer);
    for (uint16_t tile_index = 0; tile_index < CFX_MAX_TEXTURE_TILES; ++tile_index) {
        if (!tex_lut_tile_ready[tile_index]) {
            const uint8_t *tile = renderer->texture_atlas + ((int32_t)tile_index * renderer->tile_stride_bytes);
            cfx_fill_lut_from_tile(renderer, tile, tex_lut_tiles[tile_index]);
            tex_lut_tile_ready[tile_index] = 1;
        }
    }
    active_tex_lut = tex_lut_tiles[0];
}
#endif

static void cfx_renderer3d_build_lut_for_tile(const CfxRenderer3DState *renderer, const uint8_t *tile)
{
    if (tex_lut_ready && tex_lut_source_tile == tile && tex_lut_source_pitch == renderer->tile_pitch_bytes) {
        active_tex_lut = tex_lut;
        return;
    }

    cfx_fill_lut_from_tile(renderer, tile, tex_lut);

    tex_lut_source_tile = tile;
    tex_lut_source_pitch = renderer->tile_pitch_bytes;
    tex_lut_ready = 1;
    active_tex_lut = tex_lut;
}

uint32_t cfx_renderer3d_lut_size_bytes(void)
{
#if CFX_RENDERER_MULTI_LUT
    return (uint32_t)(sizeof(tex_lut) + sizeof(tex_lut_tiles));
#elif CFX_RENDERER_DIRECT_ROW_LUT
    return (uint32_t)(sizeof(tex_lut) + sizeof(tex_row_lut_tiles));
#else
    return (uint32_t)sizeof(tex_lut);
#endif
}

void cfx_renderer3d_init(CfxRenderer3D *renderer, const CfxRenderer3DConfig *config)
{
    CfxRenderer3DState *state = cfx_state(renderer);
    tex_lut_ready = 0;
    tex_lut_source_tile = NULL;
    active_tex_lut = tex_lut;
#if CFX_RENDERER_DIRECT_ROW_LUT
    tex_row_lut_ready = 0;
    tex_row_lut_source_atlas = NULL;
#endif
#if CFX_RENDERER_MULTI_LUT
    tex_lut_tiles_ready = 0;
    tex_lut_tiles_source_atlas = NULL;
#endif
    state->framebuffer = (uint8_t *)config->framebuffer;
    state->texture_atlas = NULL;
    state->width = config->width;
    state->height = config->height;
    state->tile_pitch_bytes = CFX_TEXTURE_TILE_PITCH_BYTES;
    state->tile_stride_bytes = CFX_TEXTURE_TILE_STRIDE_BYTES;
}

void cfx_renderer3d_set_framebuffer(CfxRenderer3D *renderer, void *framebuffer)
{
    cfx_state(renderer)->framebuffer = (uint8_t *)framebuffer;
}

void cfx_renderer3d_set_texture_atlas(CfxRenderer3D *renderer, const void *atlas, DEFAULT_INT tile_pitch_bytes, DEFAULT_INT tile_stride_bytes)
{
    CfxRenderer3DState *state = cfx_state(renderer);
    state->texture_atlas = (const uint8_t *)atlas;
    state->tile_pitch_bytes = tile_pitch_bytes;
    state->tile_stride_bytes = tile_stride_bytes;
    tex_lut_ready = 0;
    tex_lut_source_tile = NULL;
    active_tex_lut = tex_lut;
#if CFX_RENDERER_DIRECT_ROW_LUT
    tex_row_lut_ready = 0;
    tex_row_lut_source_atlas = NULL;
    cfx_renderer3d_build_direct_row_luts(state);
#endif
#if CFX_RENDERER_MULTI_LUT
    tex_lut_tiles_ready = 0;
    tex_lut_tiles_source_atlas = NULL;
    cfx_renderer3d_prebuild_pcfx_tile_luts(state);
#endif
}

static inline void cfx_put_even_pixel_word(uint16_t *word, uint8_t color)
{
    *word = (uint16_t)((*word & 0x00ffu) | ((uint16_t)color << 8));
}

static inline void cfx_put_odd_pixel_word(uint16_t *word, uint8_t color)
{
    *word = (uint16_t)((*word & 0xff00u) | (uint16_t)color);
}


static inline uint16_t cfx_pack_pixel_pair(uint8_t left, uint8_t right)
{
    return (uint16_t)(((uint16_t)left << 8) | (uint16_t)right);
}

static inline uint8_t cfx_fetch_texel(uint16_t state)
{
    return active_tex_lut[state];
}


static inline uint8_t cfx_clamp_u8_i32(int32_t v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static inline uint8_t cfx_fetch_texel_fp(int32_t u_fp, int32_t v_fp)
{
    return cfx_fetch_texel(cfx_pack_tex_state(
        cfx_clamp_u8_i32(u_fp >> CFX_QUAD_UV_SHIFT),
        cfx_clamp_u8_i32(v_fp >> CFX_QUAD_UV_SHIFT)));
}

static inline __attribute__((always_inline)) void cfx_draw_scanline_fast_8(uint8_t *dst, uint16_t length, uint16_t tex_state, uint16_t tex_step)
{
    uint16_t offset = tex_state;
#if CFX_RENDERER_USE_V810_ASM
    uint32_t tmp_addr;
    uint32_t tmp_color;
    const uint8_t *lut = active_tex_lut;
    __asm__ volatile (
        "cmp 0,%[length]\n"
        "be 2f\n"
        "1:\n"
        "mov %[state],%[addr]\n"
        "andi 65535,%[addr],%[addr]\n"
        "add %[lut],%[addr]\n"
        "ld.b 0[%[addr]],%[color]\n"
        "st.b %[color],0[%[dst]]\n"
        "add 1,%[dst]\n"
        "add %[step],%[state]\n"
        "andi 65535,%[state],%[state]\n"
        "add -1,%[length]\n"
        "bne 1b\n"
        "2:\n"
        : [dst] "+r" (dst), [length] "+r" (length), [state] "+r" (offset),
          [addr] "=&r" (tmp_addr), [color] "=&r" (tmp_color)
        : [lut] "r" (lut), [step] "r" (tex_step)
        : "memory");
#else
    for (uint16_t i = 0; i < length; ++i) {
        dst[i] = active_tex_lut[offset];
        offset = (uint16_t)(offset + tex_step);
    }
#endif
}


#if CFX_RENDERER_USE_SH1_ASM
static __attribute__((noinline)) void cfx_draw_scanline_fast_pair_sh1_nowrap(uint16_t *dst, uint16_t pairs, uint16_t tex_state, int16_t tex_step_linear)
{
    uint32_t c0;
    uint32_t c1;
    uint32_t c2;
    uint32_t c3;
    const uint8_t *src = active_tex_lut + tex_state;
    int32_t step = (int32_t)tex_step_linear;
    uint16_t groups = (uint16_t)(pairs >> 1);
    uint16_t rem = (uint16_t)(pairs & 1u);

    if (groups) {
        __asm__ volatile (
            "1:\n"
            /* Four texel fetches first.  This keeps the load-use distance larger
               than the old pack-immediately loop and leaves only the required
               byte->halfword packing before the two stores. */
            "mov.b @%[src],%[c0]\n"
            "add %[step],%[src]\n"
            "mov.b @%[src],%[c1]\n"
            "add %[step],%[src]\n"
            "mov.b @%[src],%[c2]\n"
            "add %[step],%[src]\n"
            "mov.b @%[src],%[c3]\n"
            "add %[step],%[src]\n"
            "shll8 %[c0]\n"
            "or %[c1],%[c0]\n"
            "mov.w %[c0],@%[dst]\n"
            "add #2,%[dst]\n"
            "shll8 %[c2]\n"
            "or %[c3],%[c2]\n"
            "mov.w %[c2],@%[dst]\n"
            "add #2,%[dst]\n"
            "add #-1,%[groups]\n"
            "mov %[groups],r0\n"
            "cmp/eq #0,r0\n"
            "bf 1b\n"
            : [dst] "+r" (dst), [src] "+r" (src), [groups] "+r" (groups),
              [c0] "=&r" (c0), [c1] "=&r" (c1), [c2] "=&r" (c2), [c3] "=&r" (c3)
            : [step] "r" (step)
            : "r0", "memory");
    }

    if (rem) {
        __asm__ volatile (
            "mov.b @%[src],%[c0]\n"
            "add %[step],%[src]\n"
            "mov.b @%[src],%[c1]\n"
            "shll8 %[c0]\n"
            "or %[c1],%[c0]\n"
            "mov.w %[c0],@%[dst]\n"
            : [dst] "+r" (dst), [src] "+r" (src),
              [c0] "=&r" (c0), [c1] "=&r" (c1)
            : [step] "r" (step)
            : "memory");
    }
}
#endif

#if CFX_RENDERER_USE_V810_ASM
static inline __attribute__((always_inline)) void cfx_draw_scanline_fast_pair_pcfx_nowrap(uint16_t *dst, uint16_t pairs, uint16_t tex_state, int16_t tex_step_linear)
{
    uint32_t c0;
    uint32_t c1;
    uint32_t c2;
    uint32_t c3;
    const uint8_t *src = active_tex_lut + tex_state;
    int32_t step = (int32_t)tex_step_linear;
    uint16_t groups = (uint16_t)(pairs >> 1);
    uint16_t rem = (uint16_t)(pairs & 1u);

    if (groups) {
        __asm__ volatile (
            "1:\n"
            /* Two packed pixel-pairs per iteration.  This is intentionally
               compact instead of fully unrolled: the hot body is four byte LUT
               fetches, four source-step adds, two minimal packs without low-byte zero-extension, and two ST.H
               writes.  No per-pixel address rebuild and no KING reads. */
            "ld.b 0[%[src]],%[c0]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c1]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c2]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c3]\n"
            "add %[step],%[src]\n"
            "shl 8,%[c0]\n"
            "or %[c1],%[c0]\n"
            "st.h %[c0],0[%[dst]]\n"
            "shl 8,%[c2]\n"
            "or %[c3],%[c2]\n"
            "st.h %[c2],2[%[dst]]\n"
            "add 4,%[dst]\n"
            "add -1,%[groups]\n"
            "bne 1b\n"
            : [dst] "+r" (dst), [src] "+r" (src), [groups] "+r" (groups),
              [c0] "=&r" (c0), [c1] "=&r" (c1), [c2] "=&r" (c2), [c3] "=&r" (c3)
            : [step] "r" (step)
            : "memory");
    }

    if (rem) {
        __asm__ volatile (
            "ld.b 0[%[src]],%[c0]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c1]\n"
            "shl 8,%[c0]\n"
            "or %[c1],%[c0]\n"
            "st.h %[c0],0[%[dst]]\n"
            : [dst] "+r" (dst), [src] "+r" (src),
              [c0] "=&r" (c0), [c1] "=&r" (c1)
            : [step] "r" (step)
            : "memory");
    }
}
#endif

#if CFX_RENDERER_DIRECT_KRAM && CFX_RENDERER_USE_V810_ASM
static inline __attribute__((always_inline)) void cfx_draw_scanline_fast_pair_pcfx_kram_nowrap(uint16_t pairs, uint16_t tex_state, int16_t tex_step_linear)
{
    uint32_t c0;
    uint32_t c1;
    uint32_t c2;
    uint32_t c3;
    uint32_t c4;
    uint32_t c5;
    uint32_t c6;
    uint32_t c7;
    const uint8_t *src = active_tex_lut + tex_state;
    int32_t step = (int32_t)tex_step_linear;
    uint16_t groups = (uint16_t)(pairs >> 2);
    uint16_t rem = (uint16_t)(pairs & 3u);

    if (groups) {
        __asm__ volatile (
            "1:\n"
            /* Eight texels -> four KING halfword stores.  The longer unroll
               reduces branch/control overhead and places all byte loads before
               byte packing, which gives the accurate V810 core more distance
               between loads and their consumers. */
            "ld.b 0[%[src]],%[c0]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c1]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c2]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c3]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c4]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c5]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c6]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c7]\n"
            "add %[step],%[src]\n"
            "shl 8,%[c0]\n"
            "or %[c1],%[c0]\n"
            "out.h %[c0],0x604[r0]\n"
            "shl 8,%[c2]\n"
            "or %[c3],%[c2]\n"
            "out.h %[c2],0x604[r0]\n"
            "shl 8,%[c4]\n"
            "or %[c5],%[c4]\n"
            "out.h %[c4],0x604[r0]\n"
            "shl 8,%[c6]\n"
            "or %[c7],%[c6]\n"
            "out.h %[c6],0x604[r0]\n"
            "add -1,%[groups]\n"
            "bne 1b\n"
            : [src] "+r" (src), [groups] "+r" (groups),
              [c0] "=&r" (c0), [c1] "=&r" (c1), [c2] "=&r" (c2), [c3] "=&r" (c3),
              [c4] "=&r" (c4), [c5] "=&r" (c5), [c6] "=&r" (c6), [c7] "=&r" (c7)
            : [step] "r" (step)
            : "memory");
    }

    while (rem-- > 0) {
        uint8_t a = *src;
        src += step;
        uint8_t b = *src;
        src += step;
        cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(a, b));
    }
}

static inline __attribute__((always_inline)) void cfx_draw_scanline_fast_pair_pcfx_kram_wrap(uint16_t pairs, uint16_t tex_state, int16_t tex_step_linear)
{
    uint32_t state = tex_state;
    uint32_t addr;
    uint32_t c0;
    uint32_t c1;
    uint32_t c2;
    uint32_t c3;
    const uint8_t *lut = active_tex_lut;
    int32_t step = (int32_t)tex_step_linear;
    uint16_t groups = (uint16_t)(pairs >> 1);
    uint16_t rem = (uint16_t)(pairs & 1u);

    if (groups) {
        __asm__ volatile (
            "1:\n"
            "mov %[state],%[addr]\n"
            "andi 65535,%[addr],%[addr]\n"
            "add %[lut],%[addr]\n"
            "ld.b 0[%[addr]],%[c0]\n"
            "add %[step],%[state]\n"
            "mov %[state],%[addr]\n"
            "andi 65535,%[addr],%[addr]\n"
            "add %[lut],%[addr]\n"
            "ld.b 0[%[addr]],%[c1]\n"
            "add %[step],%[state]\n"
            "mov %[state],%[addr]\n"
            "andi 65535,%[addr],%[addr]\n"
            "add %[lut],%[addr]\n"
            "ld.b 0[%[addr]],%[c2]\n"
            "add %[step],%[state]\n"
            "mov %[state],%[addr]\n"
            "andi 65535,%[addr],%[addr]\n"
            "add %[lut],%[addr]\n"
            "ld.b 0[%[addr]],%[c3]\n"
            "add %[step],%[state]\n"
            "andi 65535,%[state],%[state]\n"
            "shl 8,%[c0]\n"
            "or %[c1],%[c0]\n"
            "out.h %[c0],0x604[r0]\n"
            "shl 8,%[c2]\n"
            "or %[c3],%[c2]\n"
            "out.h %[c2],0x604[r0]\n"
            "add -1,%[groups]\n"
            "bne 1b\n"
            : [state] "+r" (state), [groups] "+r" (groups),
              [addr] "=&r" (addr), [c0] "=&r" (c0), [c1] "=&r" (c1),
              [c2] "=&r" (c2), [c3] "=&r" (c3)
            : [lut] "r" (lut), [step] "r" (step)
            : "memory");
    }

    if (rem) {
        __asm__ volatile (
            "mov %[state],%[addr]\n"
            "andi 65535,%[addr],%[addr]\n"
            "add %[lut],%[addr]\n"
            "ld.b 0[%[addr]],%[c0]\n"
            "add %[step],%[state]\n"
            "mov %[state],%[addr]\n"
            "andi 65535,%[addr],%[addr]\n"
            "add %[lut],%[addr]\n"
            "ld.b 0[%[addr]],%[c1]\n"
            "shl 8,%[c0]\n"
            "or %[c1],%[c0]\n"
            "out.h %[c0],0x604[r0]\n"
            : [state] "+r" (state), [addr] "=&r" (addr),
              [c0] "=&r" (c0), [c1] "=&r" (c1)
            : [lut] "r" (lut), [step] "r" (step)
            : "memory");
    }

}
#endif

static inline __attribute__((always_inline)) uint8_t cfx_span_can_linear(uint16_t pairs, uint16_t tex_state, int8_t step_u, int8_t step_v)
{
    uint32_t pixels = (uint32_t)pairs << 1;
    if (pixels <= 1) {
        return 1;
    }

    int32_t last = (int32_t)pixels - 1;
    int32_t u0 = (uint8_t)tex_state;
    int32_t v0 = (uint8_t)(tex_state >> 8);
    int32_t u1 = u0 + ((int32_t)step_u * last);
    int32_t v1 = v0 + ((int32_t)step_v * last);

    return (u1 >= 0 && u1 <= 255 && v1 >= 0 && v1 <= 255);
}

static inline __attribute__((always_inline)) void cfx_draw_scanline_safe_pair(uint16_t *dst, uint16_t pairs, uint16_t tex_state, int8_t step_u, int8_t step_v)
{
    uint8_t u = (uint8_t)tex_state;
    uint8_t v = (uint8_t)(tex_state >> 8);

    while (pairs-- > 0) {
        uint8_t c0 = cfx_fetch_texel(cfx_pack_tex_state(u, v));
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        uint8_t c1 = cfx_fetch_texel(cfx_pack_tex_state(u, v));
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        *dst++ = cfx_pack_pixel_pair(c0, c1);
    }
}

static inline __attribute__((always_inline)) void cfx_draw_scanline_fast_pair(uint16_t *dst, uint16_t pairs, uint16_t tex_state, int16_t tex_step_linear, int8_t step_u, int8_t step_v)
{
#if CFX_RENDERER_USE_V810_ASM
    if (cfx_span_can_linear(pairs, tex_state, step_u, step_v)) {
        cfx_draw_scanline_fast_pair_pcfx_nowrap(dst, pairs, tex_state, tex_step_linear);
    } else {
        cfx_draw_scanline_safe_pair(dst, pairs, tex_state, step_u, step_v);
    }
#elif CFX_RENDERER_USE_SH1_ASM
    if (cfx_span_can_linear(pairs, tex_state, step_u, step_v)) {
        cfx_draw_scanline_fast_pair_sh1_nowrap(dst, pairs, tex_state, tex_step_linear);
    } else {
        cfx_draw_scanline_safe_pair(dst, pairs, tex_state, step_u, step_v);
    }
#else
    (void)tex_step_linear;
    cfx_draw_scanline_safe_pair(dst, pairs, tex_state, step_u, step_v);
#endif
}

#if CFX_RENDERER_HEADLESS_BYTES
static inline void cfx_draw_span_headless_bytes(const CfxRenderer3DState *renderer, int16_t y, int16_t xs, int16_t span, uint16_t tex_state, int16_t tex_step_linear, int8_t step_u, int8_t step_v)
{
    if (span <= 0 || y < 0 || y >= renderer->height) {
        return;
    }

    if (xs < 0) {
        int16_t skip = (int16_t)-xs;
        if (skip >= span) {
            return;
        }
        tex_state = cfx_advance_tex_state_n(tex_state, step_u, step_v, (uint16_t)skip);
        span = (int16_t)(span - skip);
        xs = 0;
    }

    if (xs >= renderer->width) {
        return;
    }

    if ((int32_t)xs + span > renderer->width) {
        span = (int16_t)(renderer->width - xs);
    }

    if (span <= 0) {
        return;
    }

    uint8_t *dst = renderer->framebuffer + ((int32_t)y * renderer->width) + xs;

    if (cfx_span_can_linear((uint16_t)((span + 1) >> 1), tex_state, step_u, step_v)) {
        const uint8_t *src = active_tex_lut + tex_state;
        int16_t step = tex_step_linear;
        for (int16_t i = 0; i < span; ++i) {
            *dst++ = *src;
            src += step;
        }
    } else {
        uint8_t u = (uint8_t)tex_state;
        uint8_t v = (uint8_t)(tex_state >> 8);
        for (int16_t i = 0; i < span; ++i) {
            *dst++ = cfx_fetch_texel(cfx_pack_tex_state(u, v));
            u = (uint8_t)(u + (uint8_t)step_u);
            v = (uint8_t)(v + (uint8_t)step_v);
        }
    }
}
#endif

#if CFX_RENDERER_DIRECT_KRAM
static inline __attribute__((always_inline)) void cfx_draw_scanline_safe_pair_kram(uint16_t pairs, uint16_t tex_state, int8_t step_u, int8_t step_v)
{
    uint8_t u = (uint8_t)tex_state;
    uint8_t v = (uint8_t)(tex_state >> 8);

    while (pairs-- > 0) {
        uint8_t c0 = cfx_fetch_texel(cfx_pack_tex_state(u, v));
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        uint8_t c1 = cfx_fetch_texel(cfx_pack_tex_state(u, v));
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(c0, c1));
    }
}

static inline __attribute__((always_inline)) void cfx_draw_scanline_fast_pair_kram(uint16_t pairs, uint16_t tex_state, int16_t tex_step_linear, int8_t step_u, int8_t step_v)
{
#if CFX_RENDERER_USE_V810_ASM
    if (cfx_span_can_linear(pairs, tex_state, step_u, step_v)) {
        cfx_draw_scanline_fast_pair_pcfx_kram_nowrap(pairs, tex_state, tex_step_linear);
    } else {
        cfx_draw_scanline_fast_pair_pcfx_kram_wrap(pairs, tex_state, tex_step_linear);
    }
#else
    cfx_draw_scanline_safe_pair_kram(pairs, tex_state, step_u, step_v);
#endif
}

static inline void cfx_draw_span_kram(const CfxRenderer3DState *renderer, int16_t y, int16_t xs, int16_t span, uint16_t tex_state, int16_t tex_step_linear, int8_t step_u, int8_t step_v)
{
    if (span <= 0 || y < 0 || y >= renderer->height) {
        return;
    }

    if (xs < 0) {
        int16_t skip = (int16_t)-xs;
        if (skip >= span) {
            return;
        }
        tex_state = cfx_advance_tex_state_n(tex_state, step_u, step_v, (uint16_t)skip);
        span = (int16_t)(span - skip);
        xs = 0;
    }

    if (xs >= renderer->width) {
        return;
    }

    if ((int32_t)xs + span > renderer->width) {
        span = (int16_t)(renderer->width - xs);
    }

    if (span <= 0) {
        return;
    }

    cfx_pcfx_kram_seek_frame_word(((int32_t)y * (renderer->width >> 1)) + (xs >> 1));

    if (xs & 1) {
        {
            uint16_t prev_state = cfx_advance_tex_state(tex_state, (int8_t)-step_u, (int8_t)-step_v);
            uint8_t prev = cfx_fetch_texel(prev_state);
            uint8_t edge = cfx_fetch_texel(tex_state);
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(prev, edge));
        }
        tex_state = cfx_advance_tex_state(tex_state, step_u, step_v);
        --span;
    }

    if (span >= 2) {
        uint16_t pairs = (uint16_t)(span >> 1);
        cfx_draw_scanline_fast_pair_kram(pairs, tex_state, tex_step_linear, step_u, step_v);
        tex_state = cfx_advance_tex_state_n(tex_state, step_u, step_v, (uint16_t)(pairs << 1));
        span = (int16_t)(span - (int16_t)(pairs << 1));
    }

    if (span > 0) {
        {
            uint8_t edge = cfx_fetch_texel(tex_state);
            uint8_t next = cfx_fetch_texel(cfx_advance_tex_state(tex_state, step_u, step_v));
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, next));
        }
    }
}

#if CFX_RENDERER_USE_V810_ASM
static inline __attribute__((always_inline)) void cfx_draw_span_kram_nowrap_unclipped(const CfxRenderer3DState *renderer, int16_t y, int16_t xs, int16_t span, uint16_t tex_state, int16_t tex_step_linear, int8_t step_u, int8_t step_v)
{
    /* PC-FX title cube fast path: caller has already verified screen clipping
       and LUT linearity.  Avoid the generic span entry cost and the second
       cfx_span_can_linear() multiply/check for every emitted scanline. */
    cfx_pcfx_kram_seek_frame_word(((int32_t)y * (renderer->width >> 1)) + (xs >> 1));

    if (xs & 1) {
        uint16_t prev_state = cfx_advance_tex_state(tex_state, (int8_t)-step_u, (int8_t)-step_v);
        uint8_t prev = cfx_fetch_texel(prev_state);
        uint8_t edge = cfx_fetch_texel(tex_state);
        cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(prev, edge));
        tex_state = cfx_advance_tex_state(tex_state, step_u, step_v);
        --span;
    }

    if (span >= 2) {
        uint16_t pairs = (uint16_t)(span >> 1);
        cfx_draw_scanline_fast_pair_pcfx_kram_nowrap(pairs, tex_state, tex_step_linear);
        if (span & 1) {
            tex_state = cfx_advance_tex_state_n(tex_state, step_u, step_v, (uint16_t)(pairs << 1));
        }
        span = (int16_t)(span - (int16_t)(pairs << 1));
    }

    if (span > 0) {
        uint8_t edge = cfx_fetch_texel(tex_state);
        uint8_t next = cfx_fetch_texel(cfx_advance_tex_state(tex_state, step_u, step_v));
        cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, next));
    }
}

#endif


#if CFX_RENDERER_USE_V810_ASM
static inline __attribute__((always_inline)) uint8_t cfx_fp_span_in_lut_bounds(int32_t u_fp, int32_t v_fp,
                                                                              int32_t du_fp, int32_t dv_fp,
                                                                              int16_t span)
{
    int32_t last = (int32_t)span - 1;
    int32_t end_u = u_fp + du_fp * last;
    int32_t end_v = v_fp + dv_fp * last;
    return (uint8_t)(u_fp >= 0 && u_fp <= 65535 && v_fp >= 0 && v_fp <= 65535 &&
                     end_u >= 0 && end_u <= 65535 && end_v >= 0 && end_v <= 65535);
}

static inline __attribute__((always_inline)) void cfx_draw_scanline_kram_fp_exact_v810(uint16_t pairs,
                                                                                       int32_t *u_fp_io,
                                                                                       int32_t *v_fp_io,
                                                                                       int32_t du_fp,
                                                                                       int32_t dv_fp)
{
    int32_t u_fp = *u_fp_io;
    int32_t v_fp = *v_fp_io;
    const uint8_t *lut = active_tex_lut;
    uint32_t addr0;
    uint32_t addr1;
    uint32_t tmp0;
    uint32_t tmp1;
    uint32_t c0;
    uint32_t c1;

    if (pairs) {
        __asm__ volatile (
            "1:\n"
            "mov %[v],%[addr0]\n"
            "andi 65280,%[addr0],%[addr0]\n"
            "mov %[u],%[tmp0]\n"
            "shr 8,%[tmp0]\n"
            "andi 255,%[tmp0],%[tmp0]\n"
            "or %[tmp0],%[addr0]\n"
            "add %[lut],%[addr0]\n"
            "ld.b 0[%[addr0]],%[c0]\n"
            "add %[du],%[u]\n"
            "add %[dv],%[v]\n"
            "mov %[v],%[addr1]\n"
            "andi 65280,%[addr1],%[addr1]\n"
            "mov %[u],%[tmp1]\n"
            "shr 8,%[tmp1]\n"
            "andi 255,%[tmp1],%[tmp1]\n"
            "or %[tmp1],%[addr1]\n"
            "add %[lut],%[addr1]\n"
            "ld.b 0[%[addr1]],%[c1]\n"
            "add %[du],%[u]\n"
            "add %[dv],%[v]\n"
            "shl 8,%[c0]\n"
            "or %[c1],%[c0]\n"
            "out.h %[c0],0x604[r0]\n"
            "add -1,%[pairs]\n"
            "bne 1b\n"
            : [u] "+r" (u_fp), [v] "+r" (v_fp), [pairs] "+r" (pairs),
              [addr0] "=&r" (addr0), [addr1] "=&r" (addr1),
              [tmp0] "=&r" (tmp0), [tmp1] "=&r" (tmp1),
              [c0] "=&r" (c0), [c1] "=&r" (c1)
            : [du] "r" (du_fp), [dv] "r" (dv_fp), [lut] "r" (lut)
            : "memory");
    }

    *u_fp_io = u_fp;
    *v_fp_io = v_fp;
}
#endif

static inline void cfx_draw_span_kram_fp_exact(const CfxRenderer3DState *renderer, int16_t y, int16_t xs, int16_t span,
                                               int32_t u_fp, int32_t v_fp, int32_t du_fp, int32_t dv_fp)
{
    if (span <= 0 || y < 0 || y >= renderer->height) {
        return;
    }

    if (xs < 0) {
        int16_t skip = (int16_t)-xs;
        if (skip >= span) {
            return;
        }
        u_fp += du_fp * skip;
        v_fp += dv_fp * skip;
        span = (int16_t)(span - skip);
        xs = 0;
    }

    if (xs >= renderer->width) {
        return;
    }

    if ((int32_t)xs + span > renderer->width) {
        span = (int16_t)(renderer->width - xs);
    }

    if (span <= 0) {
        return;
    }

    /* KING-only fixed-point span path.

       Do not read or update the CPU foreground mirror and do not stage a row.
       KING KRAM is a 16-bit port, so odd visible edges are emitted as complete
       halfwords.  For odd 16-bit KRAM edges, duplicate the edge texel into the
       non-visible byte instead of writing transparent index 0.  Writing 0 at
       these halfword boundaries can punch transient holes into adjacent KING
       texels when the span edge lands on an odd byte. */
#if defined(PLATFORM) && PLATFORM == NECPCFX
    const int32_t row_word = (int32_t)y << 7;
#else
    const int32_t row_word = (int32_t)y * (renderer->width >> 1);
#endif
    cfx_pcfx_kram_seek_frame_word(row_word + (xs >> 1));

    if (xs & 1) {
        uint8_t edge = cfx_fetch_texel_fp(u_fp, v_fp);
        cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, edge));
        u_fp += du_fp;
        v_fp += dv_fp;
        --span;
    }

#if CFX_RENDERER_USE_V810_ASM
    if (span >= 2) {
        uint16_t pairs = (uint16_t)(span >> 1);
        cfx_draw_scanline_kram_fp_exact_v810(pairs, &u_fp, &v_fp, du_fp, dv_fp);
        span = (int16_t)(span - (int16_t)(pairs << 1));
        if (span > 0) {
            uint8_t edge = cfx_fetch_texel_fp(u_fp, v_fp);
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, edge));
        }
        return;
    }
#endif

    while (span >= 2) {
        uint8_t c0 = cfx_fetch_texel_fp(u_fp, v_fp);
        u_fp += du_fp;
        v_fp += dv_fp;
        uint8_t c1 = cfx_fetch_texel_fp(u_fp, v_fp);
        u_fp += du_fp;
        v_fp += dv_fp;
        cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(c0, c1));
        span = (int16_t)(span - 2);
    }

    if (span > 0) {
        uint8_t edge = cfx_fetch_texel_fp(u_fp, v_fp);
        cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, edge));
    }
}
#endif

#if CFX_RENDERER_MULTI_LUT
static inline void cfx_put_even_pixel_word_pcfx(uint16_t *word, uint8_t color)
{
    *word = (uint16_t)((*word & 0x00ffu) | ((uint16_t)color << 8));
}

static inline void cfx_put_odd_pixel_word_pcfx(uint16_t *word, uint8_t color)
{
    *word = (uint16_t)((*word & 0xff00u) | (uint16_t)color);
}

static inline void cfx_draw_span_pcfx_exact_words(const CfxRenderer3DState *renderer, int16_t y, int16_t xs, int16_t span, uint16_t tex_state, int16_t tex_step_linear, int8_t step_u, int8_t step_v)
{
    if (span <= 0 || y < 0 || y >= renderer->height) {
        return;
    }

    if (xs < 0) {
        int16_t skip = (int16_t)-xs;
        if (skip >= span) {
            return;
        }
        tex_state = cfx_advance_tex_state_n(tex_state, step_u, step_v, (uint16_t)skip);
        span = (int16_t)(span - skip);
        xs = 0;
    }

    if (xs >= renderer->width) {
        return;
    }

    if ((int32_t)xs + span > renderer->width) {
        span = (int16_t)(renderer->width - xs);
    }

    if (span <= 0) {
        return;
    }

    /* Keep the PC-FX/KING byte order fixed: even/left pixel in the high byte,
       odd/right pixel in the low byte.  The earlier all-pair expansion kept
       every write as a full 16-bit store, but it also drew one texel outside
       odd/even triangle edges.  At steep title-cube angles that outside texel
       could be sampled after a 16-bit UV wrap, giving the visible bottom-edge
       texture wrap at the 2400-frame title capture.

       This exact path preserves the fast LUT pair loop for the interior and
       uses at most two read/modify/write halfwords from the CPU-side staging
       framebuffer for the edge pixels.  It still never reads KING KRAM, and
       all stores remain halfword stores. */
    uint16_t *dst = (uint16_t *)renderer->framebuffer + ((int32_t)y * (renderer->width >> 1)) + (xs >> 1);

    if (xs & 1) {
        cfx_put_odd_pixel_word_pcfx(dst, cfx_fetch_texel(tex_state));
        tex_state = cfx_advance_tex_state(tex_state, step_u, step_v);
        ++dst;
        --span;
    }

    if (span >= 2) {
        uint16_t pairs = (uint16_t)(span >> 1);
        cfx_draw_scanline_fast_pair(dst, pairs, tex_state, tex_step_linear, step_u, step_v);
        tex_state = cfx_advance_tex_state_n(tex_state, step_u, step_v, (uint16_t)(pairs << 1));
        dst += pairs;
        span = (int16_t)(span - (int16_t)(pairs << 1));
    }

    if (span > 0) {
        cfx_put_even_pixel_word_pcfx(dst, cfx_fetch_texel(tex_state));
    }
}
#endif

static inline void cfx_draw_span(const CfxRenderer3DState *renderer, int16_t y, int16_t xs, int16_t span, uint16_t tex_state, int16_t tex_step_linear, int8_t step_u, int8_t step_v)
{
#if CFX_RENDERER_DIRECT_KRAM
    if (cfx_renderer3d_direct_kram_active(renderer)) {
        cfx_draw_span_kram(renderer, y, xs, span, tex_state, tex_step_linear, step_u, step_v);
        return;
    }
#endif
#if CFX_RENDERER_MULTI_LUT
    cfx_draw_span_pcfx_exact_words(renderer, y, xs, span, tex_state, tex_step_linear, step_u, step_v);
    return;
#elif CFX_RENDERER_HEADLESS_BYTES
    cfx_draw_span_headless_bytes(renderer, y, xs, span, tex_state, tex_step_linear, step_u, step_v);
    return;
#endif
    if (span <= 0 || y < 0 || y >= renderer->height) {
        return;
    }

    if (xs < 0) {
        int16_t skip = (int16_t)-xs;
        if (skip >= span) {
            return;
        }
        tex_state = cfx_advance_tex_state_n(tex_state, step_u, step_v, (uint16_t)skip);
        span = (int16_t)(span - skip);
        xs = 0;
    }

    if (xs >= renderer->width) {
        return;
    }

    if ((int32_t)xs + span > renderer->width) {
        span = (int16_t)(renderer->width - xs);
    }

    if (span <= 0) {
        return;
    }

    uint16_t *dst = (uint16_t *)renderer->framebuffer + ((int32_t)y * (renderer->width >> 1)) + (xs >> 1);

    if (xs & 1) {
        cfx_put_odd_pixel_word(dst, cfx_fetch_texel(tex_state));
        tex_state = cfx_advance_tex_state(tex_state, step_u, step_v);
        ++dst;
        --span;
    }

    if (span >= 2) {
        uint16_t pairs = (uint16_t)(span >> 1);
        cfx_draw_scanline_fast_pair(dst, pairs, tex_state, tex_step_linear, step_u, step_v);
        tex_state = cfx_advance_tex_state_n(tex_state, step_u, step_v, (uint16_t)(pairs << 1));
        dst += pairs;
        span = (int16_t)(span - (int16_t)(pairs << 1));
    }

    if (span > 0) {
        cfx_put_even_pixel_word(dst, cfx_fetch_texel(tex_state));
    }
}



static inline const CfxVertexIn *cfx_find_rect_corner_common(const CfxVertexIn *v0, const CfxVertexIn *v1,
                                                             const CfxVertexIn *v2, const CfxVertexIn *v3,
                                                             int16_t x, int16_t y)
{
    if (v0->x == x && v0->y == y) return v0;
    if (v1->x == x && v1->y == y) return v1;
    if (v2->x == x && v2->y == y) return v2;
    if (v3->x == x && v3->y == y) return v3;
    return NULL;
}

static uint8_t cfx_get_axis_rect_corners(const CfxVertexIn *v0, const CfxVertexIn *v1,
                                          const CfxVertexIn *v2, const CfxVertexIn *v3,
                                          const CfxVertexIn **tl, const CfxVertexIn **tr,
                                          const CfxVertexIn **bl, int16_t *left, int16_t *right,
                                          int16_t *top, int16_t *bottom)
{
    const CfxVertexIn *vs[4] = { v0, v1, v2, v3 };
    *left = v0->x;
    *right = v0->x;
    *top = v0->y;
    *bottom = v0->y;
    for (uint16_t i = 1; i < 4; ++i) {
        if (vs[i]->x < *left) *left = vs[i]->x;
        if (vs[i]->x > *right) *right = vs[i]->x;
        if (vs[i]->y < *top) *top = vs[i]->y;
        if (vs[i]->y > *bottom) *bottom = vs[i]->y;
    }

    *tl = cfx_find_rect_corner_common(v0, v1, v2, v3, *left, *top);
    *tr = cfx_find_rect_corner_common(v0, v1, v2, v3, *right, *top);
    *bl = cfx_find_rect_corner_common(v0, v1, v2, v3, *left, *bottom);
    return (uint8_t)(*tl && *tr && *bl && *right >= *left && *bottom >= *top);
}

static uint8_t cfx_draw_axis_rect_lut(const CfxRenderer3DState *renderer,
                                      const CfxVertexIn *v0, const CfxVertexIn *v1,
                                      const CfxVertexIn *v2, const CfxVertexIn *v3)
{
    const CfxVertexIn *tl;
    const CfxVertexIn *tr;
    const CfxVertexIn *bl;
    int16_t left, right, top, bottom;
    if (!cfx_get_axis_rect_corners(v0, v1, v2, v3, &tl, &tr, &bl, &left, &right, &top, &bottom)) {
        return 0;
    }

    int16_t width = (int16_t)(right - left + 1);
    int16_t height = (int16_t)(bottom - top + 1);
    if (width <= 0 || height <= 0) {
        return 1;
    }

    int16_t span_step_u = 0;
    int16_t span_step_v = 0;
    int16_t row_step_u = 0;
    int16_t row_step_v = 0;
    if (width > 1) {
        span_step_u = (int16_t)cfx_div_toward_zero((int16_t)(tr->u - tl->u), (int16_t)(width - 1));
        span_step_v = (int16_t)cfx_div_toward_zero((int16_t)(tr->v - tl->v), (int16_t)(width - 1));
    }
    if (height > 1) {
        row_step_u = (int16_t)cfx_div_toward_zero((int16_t)(bl->u - tl->u), (int16_t)(height - 1));
        row_step_v = (int16_t)cfx_div_toward_zero((int16_t)(bl->v - tl->v), (int16_t)(height - 1));
    }

    int16_t row_u = (int16_t)tl->u;
    int16_t row_v = (int16_t)tl->v;
    int16_t span_step_linear = cfx_pack_tex_step_linear((int8_t)span_step_u, (int8_t)span_step_v);
    for (int16_t y = top; y <= bottom; ++y) {
        cfx_draw_span(renderer, y, left, width,
            cfx_pack_tex_state((uint8_t)row_u, (uint8_t)row_v),
            span_step_linear, (int8_t)span_step_u, (int8_t)span_step_v);
        row_u = (int16_t)(row_u + row_step_u);
        row_v = (int16_t)(row_v + row_step_v);
    }
    return 1;
}

#if CFX_RENDERER_DIRECT_RECT
static inline uint8_t cfx_fetch_direct_texel(const CfxRenderer3DState *renderer, const uint8_t *tile, uint8_t u, uint8_t v)
{
    return tile[(((uint16_t)(v >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK) *
                 (uint16_t)renderer->tile_pitch_bytes) +
                ((uint16_t)(u >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK)];
}

static inline uint8_t cfx_fetch_direct_row_texel(const uint8_t *row, uint8_t u)
{
    return row[(uint16_t)(u >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK];
}

#if CFX_RENDERER_DIRECT_ROW_LUT
static inline __attribute__((always_inline)) uint8_t cfx_u_span_can_linear(uint16_t pixels, uint8_t u, int8_t step_u)
{
    if (pixels <= 1) return 1;
    int32_t last = (int32_t)pixels - 1;
    int32_t end = (int32_t)u + ((int32_t)step_u * last);
    return (uint8_t)(end >= 0 && end <= 255);
}

#if CFX_RENDERER_USE_V810_ASM
static inline __attribute__((always_inline)) void cfx_draw_row_lut_pair_pcfx_nowrap(uint16_t *dst, uint16_t pairs, const uint8_t *src, int16_t step)
{
    uint32_t c0;
    uint32_t c1;
    uint32_t c2;
    uint32_t c3;
    uint16_t groups = (uint16_t)(pairs >> 1);
    uint16_t rem = (uint16_t)(pairs & 1u);

    if (groups) {
        __asm__ volatile (
            "1:\n"
            "ld.b 0[%[src]],%[c0]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c1]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c2]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c3]\n"
            "add %[step],%[src]\n"
            "shl 8,%[c0]\n"
            "or %[c1],%[c0]\n"
            "st.h %[c0],0[%[dst]]\n"
            "shl 8,%[c2]\n"
            "or %[c3],%[c2]\n"
            "st.h %[c2],2[%[dst]]\n"
            "add 4,%[dst]\n"
            "add -1,%[groups]\n"
            "bne 1b\n"
            : [dst] "+r" (dst), [src] "+r" (src), [groups] "+r" (groups),
              [c0] "=&r" (c0), [c1] "=&r" (c1), [c2] "=&r" (c2), [c3] "=&r" (c3)
            : [step] "r" ((int32_t)step)
            : "memory");
    }

    if (rem) {
        __asm__ volatile (
            "ld.b 0[%[src]],%[c0]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c1]\n"
            "shl 8,%[c0]\n"
            "or %[c1],%[c0]\n"
            "st.h %[c0],0[%[dst]]\n"
            : [dst] "+r" (dst), [src] "+r" (src),
              [c0] "=&r" (c0), [c1] "=&r" (c1)
            : [step] "r" ((int32_t)step)
            : "memory");
    }
}
#endif

#if CFX_RENDERER_DIRECT_KRAM && CFX_RENDERER_USE_V810_ASM
static inline __attribute__((always_inline)) void cfx_draw_row_lut_pair_pcfx_kram_nowrap(uint16_t pairs, const uint8_t *src, int16_t step)
{
    uint32_t c0;
    uint32_t c1;
    uint32_t c2;
    uint32_t c3;
    uint32_t c4;
    uint32_t c5;
    uint32_t c6;
    uint32_t c7;
    uint16_t groups = (uint16_t)(pairs >> 2);
    uint16_t rem = (uint16_t)(pairs & 3u);

    if (groups) {
        __asm__ volatile (
            "1:\n"
            "ld.b 0[%[src]],%[c0]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c1]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c2]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c3]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c4]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c5]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c6]\n"
            "add %[step],%[src]\n"
            "ld.b 0[%[src]],%[c7]\n"
            "add %[step],%[src]\n"
            "shl 8,%[c0]\n"
            "or %[c1],%[c0]\n"
            "out.h %[c0],0x604[r0]\n"
            "shl 8,%[c2]\n"
            "or %[c3],%[c2]\n"
            "out.h %[c2],0x604[r0]\n"
            "shl 8,%[c4]\n"
            "or %[c5],%[c4]\n"
            "out.h %[c4],0x604[r0]\n"
            "shl 8,%[c6]\n"
            "or %[c7],%[c6]\n"
            "out.h %[c6],0x604[r0]\n"
            "add -1,%[groups]\n"
            "bne 1b\n"
            : [src] "+r" (src), [groups] "+r" (groups),
              [c0] "=&r" (c0), [c1] "=&r" (c1), [c2] "=&r" (c2), [c3] "=&r" (c3),
              [c4] "=&r" (c4), [c5] "=&r" (c5), [c6] "=&r" (c6), [c7] "=&r" (c7)
            : [step] "r" ((int32_t)step)
            : "memory");
    }

    while (rem-- > 0) {
        uint8_t a = *src;
        src += step;
        uint8_t b = *src;
        src += step;
        cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(a, b));
    }
}

#endif

static inline __attribute__((always_inline)) void cfx_draw_span_direct_tile_row_lut(
    uint16_t *dst, int16_t xs, int16_t span, const uint8_t *row, uint8_t u, int8_t step_u
#if CFX_RENDERER_DIRECT_KRAM
    , uint8_t direct_kram
#endif
)
{
#if CFX_RENDERER_DIRECT_KRAM
    if (direct_kram) {
        if (xs & 1) {
            {
                uint8_t edge = row[u];
                cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, edge));
            }
            u = (uint8_t)(u + (uint8_t)step_u);
            --span;
        }
        if (span >= 2) {
            uint16_t pairs = (uint16_t)(span >> 1);
#if CFX_RENDERER_USE_V810_ASM
            if (cfx_u_span_can_linear((uint16_t)(pairs << 1), u, step_u)) {
                cfx_draw_row_lut_pair_pcfx_kram_nowrap(pairs, row + u, (int16_t)step_u);
                u = (uint8_t)(u + (uint8_t)((int16_t)step_u * (int16_t)(pairs << 1)));
                span = (int16_t)(span - (int16_t)(pairs << 1));
            } else
#endif
            {
                while (pairs-- > 0) {
                    uint8_t c0 = row[u];
                    u = (uint8_t)(u + (uint8_t)step_u);
                    uint8_t c1 = row[u];
                    u = (uint8_t)(u + (uint8_t)step_u);
                    cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(c0, c1));
                    span = (int16_t)(span - 2);
                }
            }
        }
        if (span > 0) {
            {
            uint8_t edge = row[u];
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, edge));
        }
        }
        return;
    }
#endif
    if (xs & 1) {
        cfx_put_odd_pixel_word(dst, row[u]);
        u = (uint8_t)(u + (uint8_t)step_u);
        ++dst;
        --span;
    }

    if (span >= 2) {
        uint16_t pairs = (uint16_t)(span >> 1);
#if CFX_RENDERER_USE_V810_ASM
        if (cfx_u_span_can_linear((uint16_t)(pairs << 1), u, step_u)) {
            cfx_draw_row_lut_pair_pcfx_nowrap(dst, pairs, row + u, (int16_t)step_u);
            u = (uint8_t)(u + (uint8_t)((int16_t)step_u * (int16_t)(pairs << 1)));
            dst += pairs;
            span = (int16_t)(span - (int16_t)(pairs << 1));
        } else
#endif
        {
            while (pairs-- > 0) {
                uint8_t c0 = row[u];
                u = (uint8_t)(u + (uint8_t)step_u);
                uint8_t c1 = row[u];
                u = (uint8_t)(u + (uint8_t)step_u);
                *dst++ = cfx_pack_pixel_pair(c0, c1);
                span = (int16_t)(span - 2);
            }
        }
    }

    if (span > 0) {
        cfx_put_even_pixel_word(dst, row[u]);
    }
}
#endif

static inline __attribute__((always_inline)) void cfx_draw_span_direct_tile_row(
    const CfxRenderer3DState *renderer, const uint8_t *tile, int16_t y, int16_t xs, int16_t span,
    uint16_t tex_state, int8_t step_u)
{
    uint8_t u = (uint8_t)tex_state;
    const uint8_t v = (uint8_t)(tex_state >> 8);
    const uint8_t *row = tile + (((uint16_t)(v >> CFX_FIXED_POINT_SHIFT) & CFX_TEX_MASK) *
                                 (uint16_t)renderer->tile_pitch_bytes);
    uint16_t *dst = (uint16_t *)renderer->framebuffer + ((int32_t)y * (renderer->width >> 1)) + (xs >> 1);
#if CFX_RENDERER_DIRECT_KRAM
    uint8_t direct_kram = cfx_renderer3d_direct_kram_active(renderer);
    if (direct_kram) {
        cfx_pcfx_kram_seek_frame_word(((int32_t)y * (renderer->width >> 1)) + (xs >> 1));
    }
#endif

#if CFX_RENDERER_DIRECT_ROW_LUT
    const uint8_t *row_lut = cfx_renderer3d_direct_row_lut(renderer, tile, v);
    if (row_lut) {
        cfx_draw_span_direct_tile_row_lut(dst, xs, span, row_lut, u, step_u
#if CFX_RENDERER_DIRECT_KRAM
            , direct_kram
#endif
        );
        return;
    }
#endif

#if CFX_RENDERER_DIRECT_KRAM
    if (direct_kram) {
        if (xs & 1) {
            {
            uint8_t edge = cfx_fetch_direct_row_texel(row, u);
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, edge));
        }
            u = (uint8_t)(u + (uint8_t)step_u);
            --span;
        }
        while (span >= 4) {
            const uint8_t c0 = cfx_fetch_direct_row_texel(row, u);
            u = (uint8_t)(u + (uint8_t)step_u);
            const uint8_t c1 = cfx_fetch_direct_row_texel(row, u);
            u = (uint8_t)(u + (uint8_t)step_u);
            const uint8_t c2 = cfx_fetch_direct_row_texel(row, u);
            u = (uint8_t)(u + (uint8_t)step_u);
            const uint8_t c3 = cfx_fetch_direct_row_texel(row, u);
            u = (uint8_t)(u + (uint8_t)step_u);
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(c0, c1));
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(c2, c3));
            span = (int16_t)(span - 4);
        }
        if (span >= 2) {
            const uint8_t c0 = cfx_fetch_direct_row_texel(row, u);
            u = (uint8_t)(u + (uint8_t)step_u);
            const uint8_t c1 = cfx_fetch_direct_row_texel(row, u);
            u = (uint8_t)(u + (uint8_t)step_u);
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(c0, c1));
            span = (int16_t)(span - 2);
        }
        if (span > 0) {
            {
            uint8_t edge = cfx_fetch_direct_row_texel(row, u);
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, edge));
        }
        }
        return;
    }
#endif

    if (xs & 1) {
        cfx_put_odd_pixel_word(dst, cfx_fetch_direct_row_texel(row, u));
#if CFX_RENDERER_DIRECT_KRAM
        if (direct_kram) cfx_pcfx_kram_write_word(*dst);
#endif
        u = (uint8_t)(u + (uint8_t)step_u);
        ++dst;
        --span;
    }

    while (span >= 4) {
        const uint8_t c0 = cfx_fetch_direct_row_texel(row, u);
        u = (uint8_t)(u + (uint8_t)step_u);
        const uint8_t c1 = cfx_fetch_direct_row_texel(row, u);
        u = (uint8_t)(u + (uint8_t)step_u);
        const uint8_t c2 = cfx_fetch_direct_row_texel(row, u);
        u = (uint8_t)(u + (uint8_t)step_u);
        const uint8_t c3 = cfx_fetch_direct_row_texel(row, u);
        u = (uint8_t)(u + (uint8_t)step_u);
        dst[0] = cfx_pack_pixel_pair(c0, c1);
        dst[1] = cfx_pack_pixel_pair(c2, c3);
#if CFX_RENDERER_DIRECT_KRAM
        if (direct_kram) {
            cfx_pcfx_kram_write_word(dst[0]);
            cfx_pcfx_kram_write_word(dst[1]);
        }
#endif
        dst += 2;
        span = (int16_t)(span - 4);
    }

    if (span >= 2) {
        const uint8_t c0 = cfx_fetch_direct_row_texel(row, u);
        u = (uint8_t)(u + (uint8_t)step_u);
        const uint8_t c1 = cfx_fetch_direct_row_texel(row, u);
        u = (uint8_t)(u + (uint8_t)step_u);
        {
            uint16_t packed = cfx_pack_pixel_pair(c0, c1);
            *dst++ = packed;
#if CFX_RENDERER_DIRECT_KRAM
            if (direct_kram) cfx_pcfx_kram_write_word(packed);
#endif
        }
        span = (int16_t)(span - 2);
    }

    if (span > 0) {
        cfx_put_even_pixel_word(dst, cfx_fetch_direct_row_texel(row, u));
#if CFX_RENDERER_DIRECT_KRAM
        if (direct_kram) cfx_pcfx_kram_write_word(*dst);
#endif
    }
}

static inline void cfx_draw_span_direct_tile(const CfxRenderer3DState *renderer, const uint8_t *tile,
                                             int16_t y, int16_t xs, int16_t span,
                                             uint16_t tex_state, int8_t step_u, int8_t step_v)
{
    if (span <= 0 || y < 0 || y >= renderer->height) {
        return;
    }

    if (xs < 0) {
        int16_t skip = (int16_t)-xs;
        if (skip >= span) {
            return;
        }
        tex_state = cfx_advance_tex_state_n(tex_state, step_u, step_v, (uint16_t)skip);
        span = (int16_t)(span - skip);
        xs = 0;
    }

    if (xs >= renderer->width) {
        return;
    }

    if ((int32_t)xs + span > renderer->width) {
        span = (int16_t)(renderer->width - xs);
    }

    if (span <= 0) {
        return;
    }

    /* Axis-aligned block faces have a constant V across each row.  Keep this
       path separate so the PC-FX/V810 hot loop only shifts U, loads from a
       single 16-byte source row, packs two pixels, and stores one KING word.
       The old direct path recomputed the 2-D texel address for every pixel. */
    if (step_v == 0) {
        cfx_draw_span_direct_tile_row(renderer, tile, y, xs, span, tex_state, step_u);
        return;
    }

    uint8_t u = (uint8_t)tex_state;
    uint8_t v = (uint8_t)(tex_state >> 8);
    uint16_t *dst = (uint16_t *)renderer->framebuffer + ((int32_t)y * (renderer->width >> 1)) + (xs >> 1);
#if CFX_RENDERER_DIRECT_KRAM
    uint8_t direct_kram = cfx_renderer3d_direct_kram_active(renderer);
    if (direct_kram) {
        cfx_pcfx_kram_seek_frame_word(((int32_t)y * (renderer->width >> 1)) + (xs >> 1));
    }
#endif

#if CFX_RENDERER_DIRECT_KRAM
    if (direct_kram) {
        if (xs & 1) {
            {
            uint8_t edge = cfx_fetch_direct_texel(renderer, tile, u, v);
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, edge));
        }
            u = (uint8_t)(u + (uint8_t)step_u);
            v = (uint8_t)(v + (uint8_t)step_v);
            --span;
        }
        while (span >= 2) {
            uint8_t c0 = cfx_fetch_direct_texel(renderer, tile, u, v);
            u = (uint8_t)(u + (uint8_t)step_u);
            v = (uint8_t)(v + (uint8_t)step_v);
            uint8_t c1 = cfx_fetch_direct_texel(renderer, tile, u, v);
            u = (uint8_t)(u + (uint8_t)step_u);
            v = (uint8_t)(v + (uint8_t)step_v);
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(c0, c1));
            span = (int16_t)(span - 2);
        }
        if (span > 0) {
            {
            uint8_t edge = cfx_fetch_direct_texel(renderer, tile, u, v);
            cfx_pcfx_kram_write_word(cfx_pack_pixel_pair(edge, edge));
        }
        }
        return;
    }
#endif

    if (xs & 1) {
        cfx_put_odd_pixel_word(dst, cfx_fetch_direct_texel(renderer, tile, u, v));
#if CFX_RENDERER_DIRECT_KRAM
        if (direct_kram) cfx_pcfx_kram_write_word(*dst);
#endif
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        ++dst;
        --span;
    }

    while (span >= 2) {
        uint8_t c0 = cfx_fetch_direct_texel(renderer, tile, u, v);
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        uint8_t c1 = cfx_fetch_direct_texel(renderer, tile, u, v);
        u = (uint8_t)(u + (uint8_t)step_u);
        v = (uint8_t)(v + (uint8_t)step_v);
        {
            uint16_t packed = cfx_pack_pixel_pair(c0, c1);
            *dst++ = packed;
#if CFX_RENDERER_DIRECT_KRAM
            if (direct_kram) cfx_pcfx_kram_write_word(packed);
#endif
        }
        span = (int16_t)(span - 2);
    }

    if (span > 0) {
        cfx_put_even_pixel_word(dst, cfx_fetch_direct_texel(renderer, tile, u, v));
#if CFX_RENDERER_DIRECT_KRAM
        if (direct_kram) cfx_pcfx_kram_write_word(*dst);
#endif
    }
}

static uint8_t cfx_draw_axis_rect_direct_tile(const CfxRenderer3DState *renderer, const uint8_t *tile,
                                              const CfxVertexIn *v0, const CfxVertexIn *v1,
                                              const CfxVertexIn *v2, const CfxVertexIn *v3)
{
    const CfxVertexIn *tl;
    const CfxVertexIn *tr;
    const CfxVertexIn *bl;
    int16_t left, right, top, bottom;
    if (!cfx_get_axis_rect_corners(v0, v1, v2, v3, &tl, &tr, &bl, &left, &right, &top, &bottom)) {
        return 0;
    }

    int16_t width = (int16_t)(right - left + 1);
    int16_t height = (int16_t)(bottom - top + 1);
    if (width <= 0 || height <= 0) {
        return 1;
    }

    int16_t span_step_u = 0;
    int16_t span_step_v = 0;
    int16_t row_step_u = 0;
    int16_t row_step_v = 0;
    if (width > 1) {
        span_step_u = (int16_t)cfx_div_toward_zero((int16_t)(tr->u - tl->u), (int16_t)(width - 1));
        span_step_v = (int16_t)cfx_div_toward_zero((int16_t)(tr->v - tl->v), (int16_t)(width - 1));
    }
    if (height > 1) {
        row_step_u = (int16_t)cfx_div_toward_zero((int16_t)(bl->u - tl->u), (int16_t)(height - 1));
        row_step_v = (int16_t)cfx_div_toward_zero((int16_t)(bl->v - tl->v), (int16_t)(height - 1));
    }

    int16_t row_u = (int16_t)tl->u;
    int16_t row_v = (int16_t)tl->v;
    for (int16_t y = top; y <= bottom; ++y) {
        cfx_draw_span_direct_tile(renderer, tile, y, left, width,
            cfx_pack_tex_state((uint8_t)row_u, (uint8_t)row_v),
            (int8_t)span_step_u, (int8_t)span_step_v);
        row_u = (int16_t)(row_u + row_step_u);
        row_v = (int16_t)(row_v + row_step_v);
    }
    return 1;
}
#endif

static int cfx_left_section(CfxLeftEdge *edge)
{
    const CfxVertexIn *v1 = edge->vertices[edge->section];
    const CfxVertexIn *v2 = edge->vertices[edge->section - 1];
    int16_t height = (int16_t)(v2->y - v1->y);
    if (height == 0) {
        return 0;
    }

    edge->section_height = height;
    edge->delta_x = cfx_div_toward_zero(cfx_int_to_fixed((int32_t)v2->x - (int32_t)v1->x), height);
    edge->x = cfx_int_to_fixed(v1->x);
    edge->u = (int32_t)v1->u << CFX_EDGE_UV_SHIFT;
    edge->v = (int32_t)v1->v << CFX_EDGE_UV_SHIFT;
    edge->delta_u = cfx_div_toward_zero(((int32_t)v2->u - (int32_t)v1->u) << CFX_EDGE_UV_SHIFT, height);
    edge->delta_v = cfx_div_toward_zero(((int32_t)v2->v - (int32_t)v1->v) << CFX_EDGE_UV_SHIFT, height);
    return height;
}

static int cfx_right_section(CfxRightEdge *edge)
{
    const CfxVertexIn *v1 = edge->vertices[edge->section];
    const CfxVertexIn *v2 = edge->vertices[edge->section - 1];
    int16_t height = (int16_t)(v2->y - v1->y);
    if (height == 0) {
        return 0;
    }

    edge->section_height = height;
    edge->delta_x = cfx_div_toward_zero(cfx_int_to_fixed((int32_t)v2->x - (int32_t)v1->x), height);
    edge->x = cfx_int_to_fixed(v1->x);
    return height;
}

#if CFX_RENDERER_QUAD_SCANLINE
static void cfx_quad_build_edge(CfxQuadEdge *edge, const CfxVertexIn *a, const CfxVertexIn *b)
{
    int16_t dy = (int16_t)(b->y - a->y);
    if (dy == 0) {
        edge->y_start = a->y;
        edge->y_end = a->y;
        edge->x = cfx_int_to_fixed(a->x);
        edge->x_step = 0;
        edge->u = (int32_t)a->u << CFX_QUAD_UV_SHIFT;
        edge->v = (int32_t)a->v << CFX_QUAD_UV_SHIFT;
        edge->u_step = 0;
        edge->v_step = 0;
        return;
    }

    if (dy > 0) {
        edge->y_start = a->y;
        edge->y_end = b->y;
        edge->x = cfx_int_to_fixed(a->x);
        edge->u = (int32_t)a->u << CFX_QUAD_UV_SHIFT;
        edge->v = (int32_t)a->v << CFX_QUAD_UV_SHIFT;
        edge->x_step = cfx_div_toward_zero(cfx_int_to_fixed((int32_t)b->x - (int32_t)a->x), dy);
        edge->u_step = cfx_div_toward_zero(((int32_t)b->u - (int32_t)a->u) << CFX_QUAD_UV_SHIFT, dy);
        edge->v_step = cfx_div_toward_zero(((int32_t)b->v - (int32_t)a->v) << CFX_QUAD_UV_SHIFT, dy);
    } else {
        dy = (int16_t)-dy;
        edge->y_start = b->y;
        edge->y_end = a->y;
        edge->x = cfx_int_to_fixed(b->x);
        edge->u = (int32_t)b->u << CFX_QUAD_UV_SHIFT;
        edge->v = (int32_t)b->v << CFX_QUAD_UV_SHIFT;
        edge->x_step = cfx_div_toward_zero(cfx_int_to_fixed((int32_t)a->x - (int32_t)b->x), dy);
        edge->u_step = cfx_div_toward_zero(((int32_t)a->u - (int32_t)b->u) << CFX_QUAD_UV_SHIFT, dy);
        edge->v_step = cfx_div_toward_zero(((int32_t)a->v - (int32_t)b->v) << CFX_QUAD_UV_SHIFT, dy);
    }
}

static void cfx_draw_textured_quad_scanline(const CfxRenderer3DState *renderer,
                                            CfxVertexIn p0, CfxVertexIn p1,
                                            CfxVertexIn p2, CfxVertexIn p3)
{
    CfxVertexIn points[4] = { p0, p1, p2, p3 };
    CfxQuadEdge edges[4];
    int16_t min_y = points[0].y;
    int16_t max_y = points[0].y;

    for (int i = 1; i < 4; ++i) {
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    if (min_y == max_y) return;

    cfx_quad_build_edge(&edges[0], &points[0], &points[1]);
    cfx_quad_build_edge(&edges[1], &points[1], &points[2]);
    cfx_quad_build_edge(&edges[2], &points[2], &points[3]);
    cfx_quad_build_edge(&edges[3], &points[3], &points[0]);

#if CFX_RENDERER_DIRECT_KRAM
    const uint8_t direct_kram = cfx_renderer3d_direct_kram_active(renderer);
#else
    const uint8_t direct_kram = 0;
#endif


    if (min_y < 0) {
        int16_t skip = (int16_t)-min_y;
        for (int i = 0; i < 4; ++i) {
            if (edges[i].y_start < 0 && edges[i].y_end > 0) {
                edges[i].x += edges[i].x_step * skip;
                edges[i].u += edges[i].u_step * skip;
                edges[i].v += edges[i].v_step * skip;
                edges[i].y_start = 0;
            }
        }
        min_y = 0;
    }
    if (max_y > renderer->height) max_y = (int16_t)renderer->height;

    for (int16_t y = min_y; y < max_y; ++y) {
        int16_t count = 0;
        int32_t xs_fp[2];
        int32_t us[2];
        int32_t vs[2];

        for (int i = 0; i < 4; ++i) {
            CfxQuadEdge *e = &edges[i];
            if (y >= e->y_start && y < e->y_end) {
                if (count < 2) {
                    xs_fp[count] = e->x;
                    us[count] = e->u;
                    vs[count] = e->v;
                    ++count;
                }
                e->x += e->x_step;
                e->u += e->u_step;
                e->v += e->v_step;
            }
        }

        if (count < 2) continue;
        if (xs_fp[0] > xs_fp[1]) {
            int32_t tx = xs_fp[0]; xs_fp[0] = xs_fp[1]; xs_fp[1] = tx;
            int32_t tu = us[0]; us[0] = us[1]; us[1] = tu;
            int32_t tv = vs[0]; vs[0] = vs[1]; vs[1] = tv;
        }

        int32_t dx_fp = xs_fp[1] - xs_fp[0];
        if (dx_fp <= 0) continue;
        int16_t x_start = (int16_t)((xs_fp[0] + ((1 << CFX_GEOM_FIXED_SHIFT) - 1)) >> CFX_GEOM_FIXED_SHIFT);
        int16_t x_end = (int16_t)(xs_fp[1] >> CFX_GEOM_FIXED_SHIFT);
        int16_t span = (int16_t)(x_end - x_start + 1);
        if (span <= 0) continue;

        int32_t du_num_fp = (us[1] - us[0]) << CFX_GEOM_FIXED_SHIFT;
        int32_t dv_num_fp = (vs[1] - vs[0]) << CFX_GEOM_FIXED_SHIFT;
        int32_t du_fp = du_num_fp ? cfx_div_toward_zero(du_num_fp, dx_fp) : 0;
        int32_t dv_fp = dv_num_fp ? cfx_div_toward_zero(dv_num_fp, dx_fp) : 0;
        int32_t pixel_offset_fp = (((int32_t)x_start << CFX_GEOM_FIXED_SHIFT) + (1 << (CFX_GEOM_FIXED_SHIFT - 1))) - xs_fp[0];
        int32_t u_start = us[0] + ((du_fp * pixel_offset_fp) >> CFX_GEOM_FIXED_SHIFT);
        int32_t v_start = vs[0] + ((dv_fp * pixel_offset_fp) >> CFX_GEOM_FIXED_SHIFT);
#if CFX_RENDERER_DIRECT_KRAM
        if (direct_kram) {
            cfx_draw_span_kram_fp_exact(renderer, y, x_start, span, u_start, v_start, du_fp, dv_fp);
        } else
#endif
        {
            int8_t du = (int8_t)(du_fp >> CFX_QUAD_UV_SHIFT);
            int8_t dv = (int8_t)(dv_fp >> CFX_QUAD_UV_SHIFT);
            uint16_t tex_state = cfx_pack_tex_state(cfx_clamp_u8_i32(u_start >> CFX_QUAD_UV_SHIFT),
                                                    cfx_clamp_u8_i32(v_start >> CFX_QUAD_UV_SHIFT));
            int16_t tex_step_linear = cfx_pack_tex_step_linear(du, dv);
            cfx_draw_span(renderer, y, x_start, span, tex_state, tex_step_linear, du, dv);
        }
    }
}
#endif

static void cfx_draw_textured_triangle(const CfxRenderer3DState *renderer,
#if CFX_RENDERER_DIRECT_GENERIC_TILE && CFX_RENDERER_DIRECT_RECT
                                       const uint8_t *direct_tile,
#endif
                                       CfxVertexIn p1, CfxVertexIn p2, CfxVertexIn p3)
{
    if (p1.y > p2.y) { CfxVertexIn t = p1; p1 = p2; p2 = t; }
    if (p1.y > p3.y) { CfxVertexIn t = p1; p1 = p3; p3 = t; }
    if (p2.y > p3.y) { CfxVertexIn t = p2; p2 = p3; p3 = t; }

    int16_t y1 = p1.y;
    int16_t y2 = p2.y;
    int16_t y3 = p3.y;
    int16_t total_height = (int16_t)(y3 - y1);
    if (total_height == 0) {
        return;
    }

    int16_t temp = (int16_t)cfx_div_toward_zero((y2 - y1) << CFX_GEOM_FIXED_SHIFT, total_height);
    int32_t longest = (int32_t)temp * ((int32_t)p3.x - (int32_t)p1.x) + (((int32_t)p1.x - (int32_t)p2.x) << CFX_GEOM_FIXED_SHIFT);
    if (longest == 0) {
        return;
    }

    CfxLeftEdge left;
    CfxRightEdge right;

    if (longest < 0) {
        right.vertices[0] = &p3; right.vertices[1] = &p2; right.vertices[2] = &p1;
        right.section = 2;
        left.vertices[0] = &p3; left.vertices[1] = &p1; left.vertices[2] = &p1;
        left.section = 1;
        if (cfx_left_section(&left) <= 0) {
            return;
        }
        if (cfx_right_section(&right) <= 0) {
            --right.section;
            if (cfx_right_section(&right) <= 0) {
                return;
            }
        }
    } else {
        left.vertices[0] = &p3; left.vertices[1] = &p2; left.vertices[2] = &p1;
        left.section = 2;
        right.vertices[0] = &p3; right.vertices[1] = &p1; right.vertices[2] = &p1;
        right.section = 1;
        if (cfx_right_section(&right) <= 0) {
            return;
        }
        if (cfx_left_section(&left) <= 0) {
            --left.section;
            if (cfx_left_section(&left) <= 0) {
                return;
            }
        }
    }

    int32_t num_u = (int32_t)temp * ((int32_t)p3.u - (int32_t)p1.u) + cfx_int_to_fixed((int32_t)p1.u - (int32_t)p2.u);
    int32_t num_v = (int32_t)temp * ((int32_t)p3.v - (int32_t)p1.v) + cfx_int_to_fixed((int32_t)p1.v - (int32_t)p2.v);
    int8_t span_step_u = (int8_t)(int16_t)(num_u / longest);
    int8_t span_step_v = (int8_t)(int16_t)(num_v / longest);
    int16_t span_step_linear = cfx_pack_tex_step_linear(span_step_u, span_step_v);

    for (;;) {
        int16_t xs = (int16_t)(left.x >> CFX_GEOM_FIXED_SHIFT);
        int16_t xe = (int16_t)(right.x >> CFX_GEOM_FIXED_SHIFT);
        int16_t span = (int16_t)(xe - xs + 1);
        uint16_t tex_state = cfx_pack_tex_state((uint8_t)(left.u >> CFX_EDGE_UV_SHIFT), (uint8_t)(left.v >> CFX_EDGE_UV_SHIFT));

#if CFX_RENDERER_DIRECT_GENERIC_TILE && CFX_RENDERER_DIRECT_RECT
        if (direct_tile) {
            cfx_draw_span_direct_tile(renderer, direct_tile, y1, xs, span,
                                      tex_state, span_step_u, span_step_v);
        } else
#endif
        {
            cfx_draw_span(renderer, y1, xs, span, tex_state, span_step_linear, span_step_u, span_step_v);
        }
        ++y1;

        if (--left.section_height <= 0) {
            if (--left.section <= 0) {
                return;
            }
            if (cfx_left_section(&left) <= 0) {
                return;
            }
        } else {
            left.x += left.delta_x;
            left.u += left.delta_u;
            left.v += left.delta_v;
        }

        if (--right.section_height <= 0) {
            if (--right.section <= 0) {
                return;
            }
            if (cfx_right_section(&right) <= 0) {
                return;
            }
        } else {
            right.x += right.delta_x;
        }
    }
}

static inline uint16_t cfx_q8_to_q44(DEFAULT_INT q8)
{
    /* Source cube UVs are overwhelmingly exact tile endpoints.  Preserve the
       same endpoint mapping while avoiding V810 division in the common title
       cube path. */
    if (q8 <= 0) return 0;
    if (q8 >= (31 << 8)) return 255;
    int32_t scaled = ((int32_t)q8 * 255) / (31 << 8);
    if (scaled < 0) scaled = 0;
    if (scaled > 255) scaled = 255;
    return (uint16_t)scaled;
}

static inline CfxVertexIn cfx_make_vertex(const Point2D *p)
{
    CfxVertexIn v;
    v.x = (int16_t)p->x;
    v.y = (int16_t)p->y;
    v.u = cfx_q8_to_q44(p->u);
    v.v = cfx_q8_to_q44(p->v);
    return v;
}

void cfx_renderer3d_draw_quad(CfxRenderer3D *renderer, const Point2D *p0, const Point2D *p1, const Point2D *p2, const Point2D *p3, DEFAULT_INT tetromino_type)
{
    CfxRenderer3DState *state = cfx_state(renderer);
    if (!state->framebuffer || !state->texture_atlas) {
        return;
    }

    if (tetromino_type < 0) {
        tetromino_type = 0;
    }

    const uint8_t *tile = state->texture_atlas + ((int32_t)tetromino_type * state->tile_stride_bytes);
    CfxVertexIn v0 = cfx_make_vertex(p0);
    CfxVertexIn v1 = cfx_make_vertex(p1);
    CfxVertexIn v2 = cfx_make_vertex(p2);
    CfxVertexIn v3 = cfx_make_vertex(p3);

#if CFX_RENDERER_DIRECT_RECT
    /* Most in-game block front faces are unrotated screen-aligned rectangles.
       Draw those directly from the 16x16 source tile so Loopy does not rebuild
       the 64 KiB expanded texture LUT every time adjacent cells have different
       tetromino colors. */
    if (cfx_draw_axis_rect_direct_tile(state, tile, &v0, &v1, &v2, &v3)) {
        return;
    }
#endif

#if CFX_RENDERER_DIRECT_GENERIC_TILE && CFX_RENDERER_DIRECT_RECT
    /* The locked-board cache is rendered into an offscreen CPU bitmap.  For
       non-axis side/top/bottom faces, sample the 32x32 source tile directly
       instead of rebuilding the single expanded 64 KiB texture LUT whenever the
       tetromino color changes.  Keep the direct-KING live-piece path on the
       older LUT/FP-exact path to avoid touching foreground timing/edges. */
#if CFX_RENDERER_DIRECT_KRAM
    if (!cfx_renderer3d_direct_kram_active(state))
#endif
    {
        cfx_draw_textured_triangle(state, tile, v0, v1, v2);
        cfx_draw_textured_triangle(state, tile, v0, v2, v3);
        return;
    }
#endif

#if CFX_RENDERER_MULTI_LUT
    cfx_renderer3d_select_pcfx_tile_lut(state, tetromino_type);
#endif

#if !CFX_RENDERER_MULTI_LUT
    cfx_renderer3d_build_lut_for_tile(state, tile);
#else
    (void)tile;
#endif

#if CFX_RENDERER_QUAD_SCANLINE
    if (cfx_renderer3d_direct_kram_active(state)) {
        cfx_draw_textured_quad_scanline(state, v0, v1, v2, v3);
        return;
    }
#endif

    if (cfx_draw_axis_rect_lut(state, &v0, &v1, &v2, &v3)) {
        return;
    }

    cfx_draw_textured_triangle(state,
#if CFX_RENDERER_DIRECT_GENERIC_TILE && CFX_RENDERER_DIRECT_RECT
                               NULL,
#endif
                               v0, v1, v2);
    cfx_draw_textured_triangle(state,
#if CFX_RENDERER_DIRECT_GENERIC_TILE && CFX_RENDERER_DIRECT_RECT
                               NULL,
#endif
                               v0, v2, v3);
}

void cfx_renderer3d_draw_face_list(CfxRenderer3D *renderer, FaceToDraw *faces, DEFAULT_INT face_count)
{
    for (DEFAULT_INT i = 0; i < face_count; ++i) {
        FaceToDraw *face = faces + i;
        cfx_renderer3d_draw_quad(renderer,
            &face->projected_vertices[0],
            &face->projected_vertices[1],
            &face->projected_vertices[2],
            &face->projected_vertices[3],
            face->tetromino_type);
    }
}
