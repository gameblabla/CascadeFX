/*
 * 2024 - Gameblabla
 * LICENSE : MIT (except when stated otherwise)
*/

#include "defines.h"

#define _16BITS_WRITES
//#define BIGENDIAN_TEXTURING 1
//#define FORCE_FULLSCREEN_DRAWS 1

#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "common.h"
#include "renderer3d.h"
#include "physics.h"
#include "input_port.h"
#include "cfx_present.h"

#define FP 8
#define fp_mul(a,b) (((a)*(b))>>FP)

static inline int32_t cfx_mul_i32(DEFAULT_INT a, DEFAULT_INT b)
{
    return (int32_t)a * (int32_t)b;
}


extern void *   memcpy (void *__restrict, const void *__restrict, size_t);;

#ifdef FAST_DIV
static uint16_t div_lutr[1025];
static void init_div_lutr_fast(void) {
    int i;
    div_lutr[0] = 0;
    for(i=1;i<1025;i++){
        div_lutr[i] = (1<<FP)/i;
    }
}
#endif


#include "cfx_game_platform.h"

#define FB_SIZE 1
#define TEXTURE_SIZE_EACH CFX_TEXTURE_TILE_STRIDE_BYTES
#define DEFAULT_INTERVAL 400
#define DROP_SFX CFX_SFX_DROP
#define EXPLOSION_SFX CFX_SFX_EXPLOSION
#define SELECT_SFX CFX_SFX_SELECT
#define READY_SFX CFX_SFX_READY
#define GO_SFX CFX_SFX_GO
#define GAME_FRAMEBUFFER cfx_game_framebuffer()
#define framebuffer_game cfx_game_framebuffer()
#define REFRESH_SCREEN(index, length) cfx_game_mark_dirty((index), (length))
#define REFRESH_SCREEN_RECT(x, y, w, h) cfx_game_mark_dirty_rect((x), (y), (w), (h))
#define PRESENT_SCREEN() cfx_game_present_immediate()
#define PLAY_SFX(channel, index) cfx_game_play_sfx((index))
#define my_memcpy32 cfx_game_memcpy
#define texture cfx_game_texture_atlas()
#define bg_game cfx_game_bg_game()
#define bg_title cfx_game_bg_title()

static const uint8_t *texture_actual;

#include "font_drawing.h"
#include "trig.h"

char* myitoa(DEFAULT_INT value) {
    static char buffer[12];  // Enough for an integer (-2147483648 has 11 characters + 1 for '\0')
    char* ptr = buffer + sizeof(buffer) - 1;
    DEFAULT_INT is_negative = 0;

    // Null-terminate the buffer
    *ptr = '\0';

    // Handle negative numbers
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    // Process each digit
    do {
        *--ptr = (value % 10) + '0';
        value /= 10;
    } while (value);

    // Add the negative sign if necessary
    if (is_negative) {
        *--ptr = '-';
    }

    return ptr;
}


#include "qsort.inc"
#include "defines.h"

DEFAULT_INT force_redraw = 0;
DEFAULT_INT force_redraw_puzzle = 0;
bool backtitle = false;

// Game state variables
DEFAULT_INT grid[(GRID_HEIGHT * GRID_WIDTH)];
DEFAULT_INT score = 0;
bool game_over = false;

/* Arcade is intentionally TGM-inspired rather than a literal clone.  The
   gameplay state is still small and deterministic for SH-1/V810, but Arcade
   now has: rapid gravity progression, 20G-like instant gravity, finite lock
   delay, ARE, line clear delay, DAS/ARR, IRS, and IHS. */
static DEFAULT_INT arcade_level = 0;
static DEFAULT_INT arcade_lines_cleared = 0;
static DEFAULT_INT arcade_piece_count = 0;
static DEFAULT_INT cfx_last_lines_cleared = 0;
static DEFAULT_INT cfx_tetris_line_score(DEFAULT_INT lines);

#define CFX_ARCADE_PHASE_ACTIVE 0
#define CFX_ARCADE_PHASE_ARE    1

static uint8_t cfx_arcade_phase = CFX_ARCADE_PHASE_ACTIVE;
static int32_t cfx_arcade_phase_until_ms = 0;
static DEFAULT_INT cfx_arcade_hold_piece = -1;
static uint8_t cfx_arcade_hold_used = 0;
static uint8_t cfx_arcade_irs_buffer = 0;
static uint8_t cfx_arcade_ihs_buffer = 0;
static int32_t cfx_arcade_last_hold_ms = -100000;
static DEFAULT_INT cfx_arcade_das_dir = 0;
static int32_t cfx_arcade_das_start_ms = 0;
static int32_t cfx_arcade_arr_last_ms = 0;
static int32_t cfx_arcade_lock_started_ms = -1;
static DEFAULT_INT cfx_arcade_lock_resets = 0;
static uint8_t cfx_arcade_was_grounded = 0;

int32_t drop_interval = 30; // Drop interval in milliseconds

// Background state variables
DEFAULT_INT clear_background_frames = 0;
bool almost_losing = false;

// Current piece
typedef struct {
    DEFAULT_INT type;
    DEFAULT_INT rotation;
    DEFAULT_INT x, y;
} Piece __attribute__((aligned(4)));;

Piece current_piece;

static void cfx_rotation_anim_clear(void);
static void cfx_move_anim_clear(void);
static void cfx_rotation_anim_start(DEFAULT_INT from_x, DEFAULT_INT from_y, DEFAULT_INT from_rotation, DEFAULT_INT to_x, DEFAULT_INT to_y, DEFAULT_INT to_rotation);
void load_puzzle(DEFAULT_INT index);
void Game_Switch(DEFAULT_INT state);
bool check_collision(DEFAULT_INT new_x, DEFAULT_INT new_y, DEFAULT_INT new_rotation);

// Define macro for puzzle piece indexing
#define GET_PUZZLE_PIECE_INDEX(type, rotation, i, j) \
    ((((type) * 4 + (rotation)) * 4 + (i)) * 4 + (j))

FaceToDraw face_list[MAX_FACES];
DEFAULT_INT face_count = 0;
static CfxRenderer3D renderer3d;
static CfxPhysicsClock drop_clock;
static int32_t cfx_now_ms = 0;

/* Gameplay cubes are axis-aligned and always live on the same fixed board
   lattice.  Recomputing transform/projection/backface data for every occupied
   cell on every redraw is expensive on SH-1/V810, and it becomes pathological
   exactly when many blocks share the same scanlines.  Cache projected per-cell
   face geometry once, then only stamp the visible exterior faces each redraw. */
#define CFX_FACE_CACHE_Y_MIN (-4)
#define CFX_FACE_CACHE_ROWS (GRID_HEIGHT - CFX_FACE_CACHE_Y_MIN)

typedef struct {
    Point2D projected_vertices[4];
    DEFAULT_INT average_depth;
    uint8_t visible;
} CfxCachedCellFace;

static CfxCachedCellFace cfx_cell_faces[CFX_FACE_CACHE_ROWS][GRID_WIDTH][6];
static uint8_t cfx_cell_faces_ready = 0;

typedef struct {
    DEFAULT_INT x0;
    DEFAULT_INT y0;
    DEFAULT_INT x1;
    DEFAULT_INT y1;
    DEFAULT_INT active;
} CfxRenderDirty;

#define CFX_RENDER_DIRTY_MAX_RECTS 16

static CfxRenderDirty cfx_render_dirty_rects[CFX_RENDER_DIRTY_MAX_RECTS];
static DEFAULT_INT cfx_render_dirty_count = 0;
static CfxRenderDirty cfx_active_render_dirty_rect(DEFAULT_INT type, DEFAULT_INT rotation, DEFAULT_INT grid_x, DEFAULT_INT grid_y);

/* The locked pile is effectively static between piece locks, line clears, and
   puzzle loads.  Rendering it as 3D geometry every frame is the observed Loopy
   and PC-FX cliff when several blocks share scanlines.  Keep a pre-rendered
   2D board image here: each movement frame restores from this cache, then
   draws only the active piece. */
static uint8_t cfx_board_cache[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(4)));
static uint8_t cfx_board_cache_valid = 0;
static uint8_t cfx_board_cache_rebuild_pending = 1;
static CfxRenderDirty cfx_board_cache_visible_rect = {0, 0, 0, 0, 0};

/* Shadow repair snapshot.

   The active piece is intentionally rendered as a cheap sprite layer above the
   cached board.  To keep its shadow visible when it approaches locked blocks,
   take a small framebuffer snapshot after drawing the shadow but before drawing
   the active piece.  The final repair pass then redraws shadow pixels only
   where the active piece did not actually write.  This avoids using the cached
   board as an occlusion test, which was the source of the 1600/1650 landing
   holes, and it still preserves the active piece on top. */
static uint8_t cfx_shadow_pre_piece[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(4)));
static CfxRenderDirty cfx_shadow_pre_piece_rect = {0, 0, 0, 0, 0};
static CfxRenderDirty cfx_prev_landing_dirty_rect = {0, 0, 0, 0, 0};

static void cfx_board_cache_rebuild_to_screen(void);

static void cfx_board_cache_invalidate(void)
{
    cfx_board_cache_valid = 0;
    cfx_board_cache_rebuild_pending = 1;
    cfx_board_cache_visible_rect.active = 0;
}

static void cfx_render_dirty_reset(void)
{
    cfx_render_dirty_count = 0;
}

static inline uint8_t cfx_render_rects_touch_or_overlap(const CfxRenderDirty *a, const CfxRenderDirty *b)
{
    return (uint8_t)(a->x0 <= b->x1 && a->x1 >= b->x0 &&
                     a->y0 <= b->y1 && a->y1 >= b->y0);
}

static inline void cfx_render_dirty_merge_rect(CfxRenderDirty *dst, const CfxRenderDirty *src)
{
    if (src->x0 < dst->x0) dst->x0 = src->x0;
    if (src->y0 < dst->y0) dst->y0 = src->y0;
    if (src->x1 > dst->x1) dst->x1 = src->x1;
    if (src->y1 > dst->y1) dst->y1 = src->y1;
    dst->active = 1;
}

static void cfx_render_dirty_add_rect(DEFAULT_INT x0, DEFAULT_INT y0, DEFAULT_INT x1, DEFAULT_INT y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_WIDTH) x1 = SCREEN_WIDTH;
    if (y1 > SCREEN_HEIGHT) y1 = SCREEN_HEIGHT;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    CfxRenderDirty rect = {x0, y0, x1, y1, 1};

    /* Keep several disjoint dirty areas instead of unioning all gameplay block
       faces into one tall rectangle.  Adjacent blocks on the same scanlines now
       usually merge into a narrow row strip, while unrelated old/new piece areas
       remain separate and avoid large Loopy/PC-FX VRAM uploads. */
    for (DEFAULT_INT i = 0; i < cfx_render_dirty_count; ++i) {
        if (cfx_render_rects_touch_or_overlap(&cfx_render_dirty_rects[i], &rect)) {
            cfx_render_dirty_merge_rect(&cfx_render_dirty_rects[i], &rect);
            return;
        }
    }

    if (cfx_render_dirty_count < CFX_RENDER_DIRTY_MAX_RECTS) {
        cfx_render_dirty_rects[cfx_render_dirty_count++] = rect;
        return;
    }

    for (DEFAULT_INT i = 1; i < cfx_render_dirty_count; ++i) {
        cfx_render_dirty_merge_rect(&cfx_render_dirty_rects[0], &cfx_render_dirty_rects[i]);
    }
    cfx_render_dirty_merge_rect(&cfx_render_dirty_rects[0], &rect);
    cfx_render_dirty_count = 1;
}

static void cfx_render_dirty_add_quad(const Point2D *p)
{
    DEFAULT_INT x0 = p[0].x;
    DEFAULT_INT y0 = p[0].y;
    DEFAULT_INT x1 = p[0].x;
    DEFAULT_INT y1 = p[0].y;
    for (DEFAULT_INT i = 1; i < 4; ++i) {
        if (p[i].x < x0) x0 = p[i].x;
        if (p[i].y < y0) y0 = p[i].y;
        if (p[i].x > x1) x1 = p[i].x;
        if (p[i].y > y1) y1 = p[i].y;
    }
    cfx_render_dirty_add_rect(x0 - 2, y0 - 2, x1 + 3, y1 + 3);
}

static void cfx_render_dirty_commit(void)
{
    for (DEFAULT_INT i = 0; i < cfx_render_dirty_count; ++i) {
        const CfxRenderDirty *r = &cfx_render_dirty_rects[i];
        if (r->active) {
            REFRESH_SCREEN_RECT(r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
        }
    }
    cfx_render_dirty_reset();
}

static void cfx_render_dirty_align_x16(CfxRenderDirty *r)
{
    if (!r->active) {
        return;
    }

    r->x0 &= ~15;
    r->x1 = (r->x1 + 15) & ~15;
    if (r->x0 < 0) r->x0 = 0;
    if (r->x1 > SCREEN_WIDTH) r->x1 = SCREEN_WIDTH;
    if (r->y0 < 0) r->y0 = 0;
    if (r->y1 > SCREEN_HEIGHT) r->y1 = SCREEN_HEIGHT;
    if (r->x0 >= r->x1 || r->y0 >= r->y1) {
        r->active = 0;
    }
}

static void cfx_render_dirty_union(CfxRenderDirty *dst, const CfxRenderDirty *src)
{
    if (!src->active) {
        return;
    }
    if (!dst->active) {
        *dst = *src;
        return;
    }
    if (src->x0 < dst->x0) dst->x0 = src->x0;
    if (src->y0 < dst->y0) dst->y0 = src->y0;
    if (src->x1 > dst->x1) dst->x1 = src->x1;
    if (src->y1 > dst->y1) dst->y1 = src->y1;
}

static void cfx_restore_background_rect(const void *bg_void, const CfxRenderDirty *r)
{
    if (!r->active) {
        return;
    }

    const DEFAULT_INT width = r->x1 - r->x0;
    cfx_game_prepare_dirty_rect(r->x0, r->y0, width, r->y1 - r->y0);
#if PLATFORM == NECPCFX
    const uint8_t *bg = (const uint8_t *)bg_void;

    /* PC-FX uses the VDC layers for the static title/menu/background and KING
       as a transparent foreground overlay.  The 60 FPS title path must stay
       KING-clear-only: restoring bg_title just means clearing stale foreground
       KRAM, which cfx_game_prepare_dirty_rect() already did for this page.

       Gameplay is different.  Locked blocks live in cfx_board_cache as a 2D
       CPU-side image of the field.  When the active 3D piece moves, restore
       the affected rect from that cache and upload it as bitmap pixels to KING;
       do not rerun locked-piece polygon rasterization every frame. */
    if (bg == (const uint8_t *)cfx_board_cache) {
        for (DEFAULT_INT y = r->y0; y < r->y1; ++y) {
            my_memcpy32(GAME_FRAMEBUFFER + (y * SCREEN_WIDTH) + r->x0,
                        bg + (y * SCREEN_WIDTH) + r->x0,
                        width);
        }
        cfx_game_pcfx_restore_board_cache_rect(bg, r->x0, r->y0, width, r->y1 - r->y0);
    }
#else
    const uint8_t *bg = (const uint8_t *)bg_void;
    for (DEFAULT_INT y = r->y0; y < r->y1; ++y) {
        my_memcpy32(GAME_FRAMEBUFFER + (y * SCREEN_WIDTH) + r->x0,
                    bg + (y * SCREEN_WIDTH) + r->x0,
                    width);
    }
    cfx_game_upload_rect_pixels((const uint8_t *)GAME_FRAMEBUFFER, r->x0, r->y0, width, r->y1 - r->y0);
#endif
}

static void cfx_restore_board_rect(const CfxRenderDirty *r)
{
    cfx_restore_background_rect(cfx_board_cache_valid ? (const void *)cfx_board_cache : (const void *)bg_game, r);
}

static void cfx_shadow_snapshot_clear(void)
{
    cfx_shadow_pre_piece_rect.active = 0;
    cfx_shadow_pre_piece_rect.x0 = 0;
    cfx_shadow_pre_piece_rect.y0 = 0;
    cfx_shadow_pre_piece_rect.x1 = 0;
    cfx_shadow_pre_piece_rect.y1 = 0;
}

static void cfx_shadow_snapshot_begin(const CfxRenderDirty *r)
{
    if (!r->active) {
        cfx_shadow_snapshot_clear();
        return;
    }

    cfx_shadow_pre_piece_rect = *r;
    for (DEFAULT_INT y = r->y0; y < r->y1; ++y) {
        const DEFAULT_INT off = y * SCREEN_WIDTH + r->x0;
        my_memcpy32(cfx_shadow_pre_piece + off, GAME_FRAMEBUFFER + off, r->x1 - r->x0);
    }
}

static inline uint8_t cfx_shadow_snapshot_unchanged(DEFAULT_INT x, DEFAULT_INT y)
{
    if (!cfx_shadow_pre_piece_rect.active) {
        return 0;
    }
    if (x < cfx_shadow_pre_piece_rect.x0 || x >= cfx_shadow_pre_piece_rect.x1 ||
        y < cfx_shadow_pre_piece_rect.y0 || y >= cfx_shadow_pre_piece_rect.y1) {
        return 0;
    }

    DEFAULT_INT index = y * SCREEN_WIDTH + x;
    return (uint8_t)(GAME_FRAMEBUFFER[index] == cfx_shadow_pre_piece[index]);
}

/* Active-piece presentation layer.

   The locked board remains a pre-rendered 2D cache and is not redrawn while a
   piece is movable.  To make the active piece read as an intentional foreground
   sprite layer, draw a centered black piece-shaped shadow between the cached
   board image and the active piece.  This is strictly 2D and uses only cheap
   indexed pixel writes plus bit-pattern dithering.  The core is centered under
   the piece silhouette; the surrounding dither gives it motion without adding
   any locked-pile geometry work. */
#define CFX_ACTIVE_SHADOW_MARGIN 0
#define CFX_ACTIVE_SHADOW_CORE_INFLATE 1
#define CFX_ACTIVE_SHADOW_HALO 4
#define CFX_ACTIVE_SHADOW_DX 0
#define CFX_ACTIVE_SHADOW_DY 0
#define CFX_REMAP_BLACK_INDEX 170
#define CFX_ACTIVE_SHADOW_CORE_COLOR CFX_REMAP_BLACK_INDEX
#define CFX_ACTIVE_SHADOW_CORE_DITHER_COLOR CFX_REMAP_BLACK_INDEX
#define CFX_ACTIVE_SHADOW_RIM_COLOR CFX_REMAP_BLACK_INDEX
#define CFX_ACTIVE_SHADOW_HALO_COLOR CFX_REMAP_BLACK_INDEX
#define CFX_ACTIVE_SHADOW_MAX_RECTS 4
#define CFX_ACTIVE_SHADOW_RESTORE_PAD (CFX_ACTIVE_SHADOW_MARGIN + CFX_ACTIVE_SHADOW_CORE_INFLATE + CFX_ACTIVE_SHADOW_HALO + 2)

typedef struct {
    CfxRenderDirty rects[CFX_ACTIVE_SHADOW_MAX_RECTS];
    DEFAULT_INT count;
    CfxRenderDirty outer;
} CfxActiveShadowShape;

static inline void cfx_rect_clamp(CfxRenderDirty *r)
{
    if (!r->active) {
        return;
    }
    if (r->x0 < 0) r->x0 = 0;
    if (r->y0 < 0) r->y0 = 0;
    if (r->x1 > SCREEN_WIDTH) r->x1 = SCREEN_WIDTH;
    if (r->y1 > SCREEN_HEIGHT) r->y1 = SCREEN_HEIGHT;
    if (r->x0 >= r->x1 || r->y0 >= r->y1) {
        r->active = 0;
    }
}

static inline DEFAULT_INT cfx_rect_chebyshev_distance(DEFAULT_INT x, DEFAULT_INT y, const CfxRenderDirty *r)
{
    DEFAULT_INT dx = 0;
    DEFAULT_INT dy = 0;

    if (x < r->x0) {
        dx = r->x0 - x;
    } else if (x >= r->x1) {
        dx = x - r->x1 + 1;
    }

    if (y < r->y0) {
        dy = r->y0 - y;
    } else if (y >= r->y1) {
        dy = y - r->y1 + 1;
    }

    return (dx > dy) ? dx : dy;
}

static inline uint8_t cfx_shadow_bayer4(DEFAULT_INT x, DEFAULT_INT y, uint8_t phase)
{
    static const uint8_t bayer[16] = {
        0,  8,  2, 10,
        12, 4, 14,  6,
        3, 11,  1,  9,
        15, 7, 13,  5
    };

    /* Scroll the ordered dither lattice instead of changing its density.  The
       result is animated, but the shadow weight stays uniform on every side of
       every tetromino. */
    DEFAULT_INT sx = (x + (DEFAULT_INT)(phase & 3)) & 3;
    DEFAULT_INT sy = (y + (DEFAULT_INT)((phase >> 2) & 3)) & 3;
    return bayer[(sy << 2) | sx];
}

static inline DEFAULT_INT cfx_shadow_distance_to_shape(DEFAULT_INT x, DEFAULT_INT y,
                                                       const CfxActiveShadowShape *shape)
{
    DEFAULT_INT best = CFX_ACTIVE_SHADOW_HALO + 1;

    for (DEFAULT_INT i = 0; i < shape->count; ++i) {
        DEFAULT_INT d = cfx_rect_chebyshev_distance(x, y, &shape->rects[i]);
        if (d < best) {
            best = d;
            if (best == 0) {
                break;
            }
        }
    }

    return best;
}

static inline uint8_t cfx_active_shadow_pixel_at_dist(DEFAULT_INT dist,
                                                      DEFAULT_INT x,
                                                      DEFAULT_INT y,
                                                      uint8_t phase,
                                                      uint8_t *color)
{
    if (dist > CFX_ACTIVE_SHADOW_HALO) {
        return 0;
    }

    uint8_t b = cfx_shadow_bayer4(x, y, phase);
    if (dist == 0) {
        /* The core is intentionally opaque in the pre-piece pass.  It is not
           drawn by the final repair pass, so the active piece remains on top
           even when the cached-board path clips or refreshes adjacent pixels. */
        *color = (b == 0) ? CFX_ACTIVE_SHADOW_CORE_DITHER_COLOR : CFX_ACTIVE_SHADOW_CORE_COLOR;
        return 1;
    }

    /* Uniform ordered-dither falloff.  The thresholds are independent of
       direction and piece type, fixing bottom-heavy/contact-pad artifacts. */
    uint8_t threshold;
    if (dist == 1) {
        threshold = 15;
    } else if (dist == 2) {
        threshold = 10;
    } else if (dist == 3) {
        threshold = 6;
    } else {
        threshold = 3;
    }
    if (b >= threshold) {
        return 0;
    }

    *color = (dist <= 2) ? CFX_ACTIVE_SHADOW_RIM_COLOR : CFX_ACTIVE_SHADOW_HALO_COLOR;
    return 1;
}

static inline uint8_t cfx_active_shadow_pixel(const CfxActiveShadowShape *shape,
                                              DEFAULT_INT x,
                                              DEFAULT_INT y,
                                              uint8_t phase,
                                              uint8_t *color)
{
    DEFAULT_INT dist = cfx_shadow_distance_to_shape(x, y, shape);
    return cfx_active_shadow_pixel_at_dist(dist, x, y, phase, color);
}

static void cfx_draw_active_shadow_shape(const CfxActiveShadowShape *shape, uint8_t phase)
{
    if (!shape->outer.active || shape->count <= 0) {
        return;
    }

    cfx_game_prepare_dirty_rect(shape->outer.x0, shape->outer.y0,
                                shape->outer.x1 - shape->outer.x0,
                                shape->outer.y1 - shape->outer.y0);
    for (DEFAULT_INT y = shape->outer.y0; y < shape->outer.y1; ++y) {
        DEFAULT_INT index = y * SCREEN_WIDTH + shape->outer.x0;
        for (DEFAULT_INT x = shape->outer.x0; x < shape->outer.x1; ++x, ++index) {
            uint8_t color;
            if (cfx_active_shadow_pixel(shape, x, y, phase, &color)) {
                GAME_FRAMEBUFFER[index] = color;
            }
        }
    }
    cfx_game_upload_rect_pixels((const uint8_t *)GAME_FRAMEBUFFER, shape->outer.x0, shape->outer.y0,
                                shape->outer.x1 - shape->outer.x0,
                                shape->outer.y1 - shape->outer.y0);
}

static CfxRenderDirty cfx_shift_rect(CfxRenderDirty r, DEFAULT_INT dx, DEFAULT_INT dy)
{
    if (!r.active) {
        return r;
    }
    r.x0 += dx;
    r.x1 += dx;
    r.y0 += dy;
    r.y1 += dy;
    cfx_rect_clamp(&r);
    return r;
}

static void cfx_mark_text_dirty(const char *str, DEFAULT_INT x, DEFAULT_INT y)
{
    DEFAULT_INT maxw = 0;
    DEFAULT_INT linew = 0;
    DEFAULT_INT lines = 1;
    while (*str) {
        if (*str == '\n') {
            if (linew > maxw) maxw = linew;
            linew = 0;
            ++lines;
        } else {
            linew += 8;
        }
        ++str;
    }
    if (linew > maxw) maxw = linew;
    cfx_render_dirty_add_rect(x - 1, y - 1, x + maxw + 1, y + (lines * 8) + 1);
}

/* HUD pixels must never use palette index 0 on PC-FX.  With the VDCs now
   owning the static gameplay background, KING index 0 is transparent and shows
   the VDC backdrop.  The old text outline used index 0, so small non-text holes
   around glyphs revealed moving/quantized VDC pixels in the top-left gameplay
   HUD.  Draw a small opaque cockpit panel first and use a nonzero outline. */
/* Dynamic UI colors must use post-remap palette indices.  Iteration 4
   moved texture colors below 128, so the old source-palette literals here
   landed on orange texture colors.  Keep glyph outlines and active-piece
   shadows on a reserved nonzero black, while preserving the HUD panel's
   original blue/gray indices after remap. */
#ifndef CFX_REMAP_BLACK_INDEX
#define CFX_REMAP_BLACK_INDEX 170
#endif
#define CFX_TEXT_OUTLINE_INDEX CFX_REMAP_BLACK_INDEX
#define CFX_UI_PANEL_FILL_INDEX 99
#define CFX_UI_PANEL_FILL_ALT_INDEX 35
#define CFX_UI_PANEL_RIM_INDEX 34
#define CFX_UI_PANEL_HILITE_INDEX 255
#define CFX_UI_PANEL_SHADOW_INDEX 156

static void cfx_fill_rect_index(DEFAULT_INT x, DEFAULT_INT y, DEFAULT_INT w, DEFAULT_INT h, uint8_t color)
{
    CfxRenderDirty r = {x, y, x + w, y + h, 1};
    cfx_rect_clamp(&r);
    if (!r.active) {
        return;
    }

    cfx_game_prepare_dirty_rect(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    for (DEFAULT_INT yy = r.y0; yy < r.y1; ++yy) {
        uint8_t *dst = GAME_FRAMEBUFFER + (yy * SCREEN_WIDTH) + r.x0;
        DEFAULT_INT count = r.x1 - r.x0;
        while (count-- > 0) {
            *dst++ = color;
        }
    }
    cfx_game_upload_rect_pixels((const uint8_t *)GAME_FRAMEBUFFER, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    cfx_render_dirty_add_rect(r.x0, r.y0, r.x1, r.y1);
}

static void cfx_draw_ui_panel(DEFAULT_INT x, DEFAULT_INT y, DEFAULT_INT w, DEFAULT_INT h)
{
    CfxRenderDirty r = {x, y, x + w, y + h, 1};
    cfx_rect_clamp(&r);
    if (!r.active) {
        return;
    }

    cfx_game_prepare_dirty_rect(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    for (DEFAULT_INT yy = r.y0; yy < r.y1; ++yy) {
        uint8_t *dst = GAME_FRAMEBUFFER + (yy * SCREEN_WIDTH) + r.x0;
        for (DEFAULT_INT xx = r.x0; xx < r.x1; ++xx, ++dst) {
            uint8_t color = (((xx + yy) & 7) == 0) ? CFX_UI_PANEL_FILL_ALT_INDEX : CFX_UI_PANEL_FILL_INDEX;
            if (xx == r.x0 || yy == r.y0) {
                color = CFX_UI_PANEL_HILITE_INDEX;
            } else if (xx == r.x1 - 1 || yy == r.y1 - 1) {
                color = CFX_UI_PANEL_SHADOW_INDEX;
            } else if (xx == r.x0 + 1 || yy == r.y0 + 1 ||
                       xx == r.x1 - 2 || yy == r.y1 - 2) {
                color = CFX_UI_PANEL_RIM_INDEX;
            }
            *dst = color;
        }
    }

    /* Small notched lower-right corner.  Draw it into the same prepared panel
       instead of calling the generic fill/upload path a second time. */
    {
        DEFAULT_INT nx0 = r.x1 - 10;
        DEFAULT_INT ny0 = r.y1 - 5;
        DEFAULT_INT nx1 = nx0 + 8;
        DEFAULT_INT ny1 = ny0 + 3;
        if (nx0 < r.x0) nx0 = r.x0;
        if (ny0 < r.y0) ny0 = r.y0;
        if (nx1 > r.x1) nx1 = r.x1;
        if (ny1 > r.y1) ny1 = r.y1;
        for (DEFAULT_INT yy = ny0; yy < ny1; ++yy) {
            uint8_t *dst = GAME_FRAMEBUFFER + (yy * SCREEN_WIDTH) + nx0;
            for (DEFAULT_INT xx = nx0; xx < nx1; ++xx) {
                *dst++ = CFX_UI_PANEL_RIM_INDEX;
            }
        }
    }
    cfx_game_upload_rect_pixels((const uint8_t *)GAME_FRAMEBUFFER, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    cfx_render_dirty_add_rect(r.x0, r.y0, r.x1, r.y1);
}

static void cfx_draw_gameplay_hud_panel(uint8_t puzzle_mode)
{
    if (puzzle_mode) {
        cfx_draw_ui_panel(4, 4, 62, 42);
    } else {
        cfx_draw_ui_panel(4, 4, 76, 24);
    }
}

/* Texture tile geometry lives in renderer3d.h. */

// Comparator function for qsort
static inline DEFAULT_INT compare_faces(const void *a, const void *b) {
    const FaceToDraw *faceA = (const FaceToDraw *)a;
    const FaceToDraw *faceB = (const FaceToDraw *)b;
    return faceA->average_depth - faceB->average_depth; // Sort from farthest to nearest
}

#include "raster_port.inc"


static inline void drawTexturedQuad(Point2D p0, Point2D p1, Point2D p2, Point2D p3) 
{
    // Use the vertices as given
    Point2D points_array[4] = { p0, p1, p2, p3 };
    Point2D *points = points_array;

    // Unrolled min_y and max_y calculation
    DEFAULT_INT min_y = points->y, max_y = points->y;
    if ((points + 1)->y < min_y) min_y = (points + 1)->y; else if ((points + 1)->y > max_y) max_y = (points + 1)->y;
    if ((points + 2)->y < min_y) min_y = (points + 2)->y; else if ((points + 2)->y > max_y) max_y = (points + 2)->y;
    if ((points + 3)->y < min_y) min_y = (points + 3)->y; else if ((points + 3)->y > max_y) max_y = (points + 3)->y;
    
	if (min_y <= 0) min_y = 0;
	//if (max_y >= SCREEN_HEIGHT) max_y = SCREEN_HEIGHT;
	
    EdgeData edges_array[4];
    EdgeData *edges = edges_array;

    // Precompute edge data and initialize edge positions in a single loop
    for (int16_t i = 0; i < 4; i++) {
        Point2D *pA = points + i;
        Point2D *pB = points + ((i + 1) & 3);  // Use bitwise AND to avoid modulo operation

        DEFAULT_INT dy = pB->y - pA->y;
        EdgeData *edge = edges + i;

        if (dy == 0) {
            edge->y_start = edge->y_end = pA->y;
            edge->x = INT_TO_FIXED(pA->x);
            edge->u = pA->u;
            edge->v = pA->v;
            edge->x_step = edge->u_step = edge->v_step = 0;
            continue; // Skip horizontal edges
        }

        if (dy > 0) {
            edge->y_start = pA->y;
            edge->y_end = pB->y;
            edge->x = INT_TO_FIXED(pA->x);
            edge->u = pA->u;
            edge->v = pA->v;
            
            DEFAULT_INT dx = pB->x - pA->x;
            DEFAULT_INT du = pB->u - pA->u;
			DEFAULT_INT dv = pB->v - pA->v;
#ifdef FAST_DIV
			int32_t rdy = div_lutr[dy];
			edge->x_step = fp_mul((INT_TO_FIXED(pB->x - pA->x)) << FP, rdy) >> FP;
            edge->u_step = fp_mul((pB->u - pA->u) << FP, rdy) >> FP;
            edge->v_step = fp_mul((pB->v - pA->v) << FP, rdy) >> FP;
#else
            edge->x_step = Division(INT_TO_FIXED(pB->x - pA->x) , dy);
            edge->u_step = Division(pB->u - pA->u , dy);
            edge->v_step = Division(pB->v - pA->v , dy);
#endif
        } else {
            edge->y_start = pB->y;
            edge->y_end = pA->y;
            edge->x = INT_TO_FIXED(pB->x);
            edge->u = pB->u;
            edge->v = pB->v;

            dy = -dy; // Make dy positive
			DEFAULT_INT dx = pA->x - pB->x;
			DEFAULT_INT du = pA->u - pB->u;
			DEFAULT_INT dv = pA->v - pB->v;
			
#ifdef FAST_DIV
			int32_t rdy = div_lutr[dy];
			edge->x_step = fp_mul((INT_TO_FIXED(pA->x - pB->x)) << FP, rdy) >> FP;
            edge->u_step = fp_mul((pA->u - pB->u) << FP, rdy) >> FP;
            edge->v_step = fp_mul((pA->v - pB->v) << FP, rdy) >> FP;
#else
			edge->x_step = Division(INT_TO_FIXED(pA->x - pB->x), dy);
            edge->u_step = Division(pA->u - pB->u , dy);
            edge->v_step = Division(pA->v - pB->v , dy);
#endif
        }
    }
	
	
    // For each scanline from min_y to max_y
    for (DEFAULT_INT y = min_y; y <= max_y; y++) {
        DEFAULT_INT num_intersections = 0;
        DEFAULT_INT x_intersections[4], u_intersections[4], v_intersections[4];
        DEFAULT_INT *x_int_ptr = x_intersections;
        DEFAULT_INT *u_int_ptr = u_intersections;
        DEFAULT_INT *v_int_ptr = v_intersections;

        // Process edges and update positions in a single loop
        for (int16_t i = 0; i < 4; i++) {
            EdgeData *edge = edges + i;
            if (y >= edge->y_start) 
            {
				if (y < edge->y_end)
				{
					// Store intersection data
					*x_int_ptr++ = FIXED_TO_INT(edge->x);
					*u_int_ptr++ = edge->u;
					*v_int_ptr++ = edge->v;
					num_intersections++;

					// Update edge positions for the next scanline
					edge->x += edge->x_step;
					edge->u += edge->u_step;
					edge->v += edge->v_step;
				}
            }
        }

        if (num_intersections != 2) {
            continue;
        }

        // Pointers to the first two intersections
        DEFAULT_INT *x0 = x_intersections;
        DEFAULT_INT *x1 = x_intersections + 1;
        DEFAULT_INT *u0 = u_intersections;
        DEFAULT_INT *u1 = u_intersections + 1;
        DEFAULT_INT *v0 = v_intersections;
        DEFAULT_INT *v1 = v_intersections + 1;

        // Sort intersections by x-coordinate
        if (*x0 > *x1) {
            SWAP(*x0, *x1);
            SWAP(*u0, *u1);
            SWAP(*v0, *v1);
        }

        // Draw span between the two intersections
        DEFAULT_INT xs = *x0;
        DEFAULT_INT xe = *x1;
        DEFAULT_INT us = *u0;
        DEFAULT_INT vs = *v0;
        DEFAULT_INT ue = *u1;
        DEFAULT_INT ve = *v1;

        DEFAULT_INT dx = xe - xs;
        if (dx == 0) continue;  // Avoid division by zero


#ifdef FAST_DIV
		int32_t rdx = div_lutr[dx];
		DEFAULT_INT du = fp_mul((ue - us) << FP, rdx) >> FP;
		DEFAULT_INT dv = fp_mul((ve - vs) << FP, rdx) >> FP;		
#else
		DEFAULT_INT du = Division(ue - us, dx);
		DEFAULT_INT dv = Division(ve - vs, dx);
#endif   

		drawScanline(xs, xe, us, vs, du, dv, y);
    }
}


static inline uint8_t cfx_face_before(const FaceToDraw *a, const FaceToDraw *b)
{
    if (a->average_depth != b->average_depth) {
        return a->average_depth < b->average_depth;
    }

    /* Deterministic tie-breaks keep equal-depth board faces from exercising
       the old quicksort worst case.  A full row of adjacent front faces often
       has identical depth, which made the previous pivot-at-end quicksort crawl
       despite the rasterizer itself being idle for many of those faces. */
    if (a->projected_vertices[0].y != b->projected_vertices[0].y) {
        return a->projected_vertices[0].y < b->projected_vertices[0].y;
    }
    if (a->projected_vertices[0].x != b->projected_vertices[0].x) {
        return a->projected_vertices[0].x < b->projected_vertices[0].x;
    }
    return a->tetromino_type < b->tetromino_type;
}

static void cfx_sort_faces_shell(FaceToDraw *faces, DEFAULT_INT count)
{
    for (DEFAULT_INT gap = count >> 1; gap > 0; gap >>= 1) {
        for (DEFAULT_INT i = gap; i < count; ++i) {
            FaceToDraw temp = faces[i];
            DEFAULT_INT j = i;
            while (j >= gap && cfx_face_before(&temp, &faces[j - gap])) {
                faces[j] = faces[j - gap];
                j -= gap;
            }
            faces[j] = temp;
        }
    }
}

static inline void draw_sorted_faces() 
{
	// Specialized non-recursive sort.  It is much safer than the previous
	// generic quicksort for many equal-depth block faces on one scanline.
	cfx_sort_faces_shell(face_list, face_count);
	cfx_renderer3d_draw_face_list(&renderer3d, face_list, face_count);
}






// Rotate a poDEFAULT_INT around the X-axis
Point3D rotateX(Point3D p, DEFAULT_INT angle) {
    int16_t sinA = sin_lookup[angle & ANGLE_MASK];
    int16_t cosA = cos_lookup[angle & ANGLE_MASK];

    int32_t y = FIXED_TO_INT(cfx_mul_i32(p.y , cosA) - cfx_mul_i32(p.z , sinA));
    int32_t z = FIXED_TO_INT(cfx_mul_i32(p.y , sinA) + cfx_mul_i32(p.z , cosA));
    return (Point3D){p.x, y, z};
}

// Rotate a poDEFAULT_INT around the Y-axis
Point3D rotateY(Point3D p, DEFAULT_INT angle) {
    int16_t sinA = sin_lookup[angle & ANGLE_MASK];
    int16_t cosA = cos_lookup[angle & ANGLE_MASK];

    int32_t x = FIXED_TO_INT(cfx_mul_i32(p.x , cosA) + cfx_mul_i32(p.z , sinA));
    int32_t z = FIXED_TO_INT(cfx_mul_i32(p.z , cosA) - cfx_mul_i32(p.x , sinA));
    return (Point3D){x, p.y, z};
}

// Rotate a poDEFAULT_INT around the Z-axis
Point3D rotateZ(Point3D p, DEFAULT_INT angle) {
    int16_t sinA = sin_lookup[angle & ANGLE_MASK];
    int16_t cosA = cos_lookup[angle & ANGLE_MASK];

    int32_t x = FIXED_TO_INT(cfx_mul_i32(p.x , cosA) - cfx_mul_i32(p.y , sinA));
    int32_t y = FIXED_TO_INT(cfx_mul_i32(p.x , sinA) + cfx_mul_i32(p.y , cosA));
    return (Point3D){x, y, p.z};
}

Point2D project(Point3D p, DEFAULT_INT u, DEFAULT_INT v) {
    DEFAULT_INT distance = PROJECTION_DISTANCE;
    
	DEFAULT_INT factor = Division(INT_TO_FIXED(distance) , (distance - p.z));
    
    int32_t x = FIXED_TO_INT(cfx_mul_i32(p.x , factor));
    int32_t y = FIXED_TO_INT(cfx_mul_i32(p.y , factor));

    return (Point2D){x, y, u, v};
}


// Structure to store previous piece position
typedef struct {
    DEFAULT_INT x;
    DEFAULT_INT y;
    DEFAULT_INT type;
    DEFAULT_INT rotation;
} PreviousPieceState;

PreviousPieceState previous_piece_state;

typedef struct {
    DEFAULT_INT x;
    DEFAULT_INT y;
} CfxPieceCell;

#define CFX_ROTATION_ANIM_DURATION_MS 72
#define CFX_ROTATION_ANIM_INITIAL_LEAD_MS 14
#define CFX_ROTATION_ANIM_DIRTY_MARGIN 40
#define CFX_MOVE_ANIM_DURATION_MS 48
#define CFX_MOVE_ANIM_INITIAL_LEAD_MS 16
#define CFX_MOVE_ANIM_DIRTY_MARGIN 24

typedef struct {
    uint8_t active;
    DEFAULT_INT type;
    DEFAULT_INT from_rotation;
    DEFAULT_INT to_rotation;
    DEFAULT_INT from_x;
    DEFAULT_INT from_y;
    DEFAULT_INT to_x;
    DEFAULT_INT to_y;
    DEFAULT_INT angle_sign;
    int32_t start_ms;
} CfxRotationAnimation;

typedef struct {
    uint8_t active;
    DEFAULT_INT type;
    DEFAULT_INT rotation;
    DEFAULT_INT from_x;
    DEFAULT_INT from_y;
    DEFAULT_INT to_x;
    DEFAULT_INT to_y;
    int32_t from_world_x256;
    int32_t from_world_y256;
    int32_t to_world_x256;
    int32_t to_world_y256;
    int32_t start_ms;
} CfxMoveAnimation;

static CfxRotationAnimation cfx_rotation_anim = {0, 0, 0, 0, 0, 0, 0, 0, -1, 0};
static CfxMoveAnimation cfx_move_anim = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static CfxRenderDirty cfx_prev_active_draw_rect = {0, 0, 0, 0, 0};

// Macros for maximum sizes
#define MAX_PIECE_VERTICES (8 * 4) // Max 4 blocks * 8 vertices per block
#define MAX_PIECE_FACES    (6 * 4) // Max 4 blocks * 6 faces per block

static inline void transform_cube_block(Point3D *transformed_vertices, DEFAULT_INT x_offset, DEFAULT_INT y_offset, DEFAULT_INT z_offset /*, DEFAULT_INT angle_x, DEFAULT_INT angle_y*/) {
    for (DEFAULT_INT k = 0; k < 8; k++) {
        Point3D v = cube_vertices_template[k];
        v.x += x_offset;
        v.y += y_offset;
        v.z += z_offset;

        // Apply rotations if needed
        // v = rotateX(v, angle_x);
        // v = rotateY(v, angle_y);

        transformed_vertices[k] = v;
    }
}

static void cfx_precompute_cell_faces(void)
{
    if (cfx_cell_faces_ready) {
        return;
    }

    for (DEFAULT_INT cache_y = 0; cache_y < CFX_FACE_CACHE_ROWS; ++cache_y) {
        DEFAULT_INT grid_y = cache_y + CFX_FACE_CACHE_Y_MIN;
        for (DEFAULT_INT grid_x = 0; grid_x < GRID_WIDTH; ++grid_x) {
            DEFAULT_INT x_offset = grid_x * CUBE_SIZE - (GRID_WIDTH * CUBE_SIZE) / 2;
            DEFAULT_INT y_offset = (GRID_HEIGHT - (grid_y + 1)) * CUBE_SIZE - (GRID_HEIGHT * CUBE_SIZE) / 2;
            DEFAULT_INT z_offset = STARTING_Z_OFFSET;
            Point3D transformed_vertices[8];

            transform_cube_block(transformed_vertices, x_offset, y_offset, z_offset);

            for (DEFAULT_INT m = 0; m < 6; ++m) {
                CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][grid_x][m];
                DEFAULT_INT idx0 = cube_faces_template[m * 4 + 0];
                DEFAULT_INT idx1 = cube_faces_template[m * 4 + 1];
                DEFAULT_INT idx2 = cube_faces_template[m * 4 + 2];
                DEFAULT_INT idx3 = cube_faces_template[m * 4 + 3];
                Point3D *v0 = &transformed_vertices[idx0];
                Point3D *v1 = &transformed_vertices[idx1];
                Point3D *v2 = &transformed_vertices[idx2];

                int32_t ax = v1->x - v0->x;
                int32_t ay = v1->y - v0->y;
                int32_t bx = v2->x - v0->x;
                int32_t by = v2->y - v0->y;
                int32_t nz = cfx_mul_i32(ax, by) - cfx_mul_i32(ay, bx);

                cached->visible = (uint8_t)(nz <= 0);
                cached->average_depth = (DEFAULT_INT)((v0->z + v1->z + v2->z + transformed_vertices[idx3].z) / 4);

                if (cached->visible) {
                    for (DEFAULT_INT n = 0; n < 4; ++n) {
                        DEFAULT_INT vi = cube_faces_template[m * 4 + n];
                        Point3D *v = &transformed_vertices[vi];
                        Point2D p = project(*v,
                            cube_texcoords_template[m * 8 + n * 2 + 0],
                            cube_texcoords_template[m * 8 + n * 2 + 1]);
                        p.x += SCREEN_WIDTH_HALF;
                        p.y += SCREEN_HEIGHT_HALF;
                        cached->projected_vertices[n] = p;
                    }
                }
            }
        }
    }

    cfx_cell_faces_ready = 1;
}

static inline uint8_t cfx_grid_cell_occupied(DEFAULT_INT x, DEFAULT_INT y)
{
    return (uint8_t)(x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT &&
        grid[y * GRID_WIDTH + x] != 0);
}

static inline uint8_t cfx_current_piece_cell_occupied(DEFAULT_INT x, DEFAULT_INT y)
{
    DEFAULT_INT lx = x - current_piece.x;
    DEFAULT_INT ly = y - current_piece.y;
    if (lx < 0 || lx >= 4 || ly < 0 || ly >= 4) {
        return 0;
    }
    return (uint8_t)(puzzle_pieces[GET_PUZZLE_PIECE_INDEX(current_piece.type, current_piece.rotation, ly, lx)] != 0);
}

typedef uint8_t (*CfxCellOccupiedFn)(DEFAULT_INT x, DEFAULT_INT y);

static inline uint8_t cfx_face_neighbor_occluded(DEFAULT_INT face_index, DEFAULT_INT x, DEFAULT_INT y, CfxCellOccupiedFn occupied)
{
    switch (face_index) {
        case 2: return occupied((DEFAULT_INT)(x - 1), y); /* left */
        case 3: return occupied((DEFAULT_INT)(x + 1), y); /* right */
        case 4: return occupied(x, (DEFAULT_INT)(y - 1)); /* top */
        case 5: return occupied(x, (DEFAULT_INT)(y + 1)); /* bottom */
        default: return 0;
    }
}

static inline void cfx_emit_cached_cell_faces(DEFAULT_INT x, DEFAULT_INT y, DEFAULT_INT tetromino_type, CfxCellOccupiedFn occupied)
{
    DEFAULT_INT cache_y = y - CFX_FACE_CACHE_Y_MIN;
    if ((uint16_t)x >= GRID_WIDTH || (uint16_t)cache_y >= CFX_FACE_CACHE_ROWS) {
        return;
    }

    for (DEFAULT_INT m = 0; m < 6; ++m) {
        CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][x][m];
        if (!cached->visible) {
            continue;
        }
        if (cfx_face_neighbor_occluded(m, x, y, occupied)) {
            continue;
        }
        if (face_count >= MAX_FACES) {
            return;
        }

        FaceToDraw *face_to_draw = face_list + face_count++;
        face_to_draw->projected_vertices[0] = cached->projected_vertices[0];
        face_to_draw->projected_vertices[1] = cached->projected_vertices[1];
        face_to_draw->projected_vertices[2] = cached->projected_vertices[2];
        face_to_draw->projected_vertices[3] = cached->projected_vertices[3];
        face_to_draw->average_depth = cached->average_depth;
        face_to_draw->tetromino_type = tetromino_type;
        cfx_render_dirty_add_quad(face_to_draw->projected_vertices);
    }
}


/* Fast locked-board rebuild path.

   Line clears can leave the board nearly full.  The generic rebuild path emits
   a large face list, sorts it, then feeds the generic quad rasterizer.  That is
   correct, but on V810 it is far too expensive for the one frame where rows
   shift.  The locked field has a fixed camera and fixed per-cell geometry, so
   we can draw cached faces directly in a deterministic painter order and skip
   the face-list sort entirely.  This is only for the CPU-side 2D board cache;
   active pieces still use the live 3D path. */
static inline uint8_t cfx_grid_face_neighbor_occluded_fast(DEFAULT_INT face_index,
                                                               DEFAULT_INT x,
                                                               DEFAULT_INT y)
{
    /* Full locked-board rebuilds are always sourced from grid[].  Avoid the
       generic CfxCellOccupiedFn callback here: a dense one-line clear can test
       several hundred cached faces, and the indirect call overhead is visible
       on V810 before the row shift has even reached the next vblank. */
    switch (face_index) {
        case 2: return (uint8_t)(x > 0 && grid[y * GRID_WIDTH + (x - 1)]);              /* left */
        case 3: return (uint8_t)(x + 1 < GRID_WIDTH && grid[y * GRID_WIDTH + (x + 1)]); /* right */
        case 4: return (uint8_t)(y > 0 && grid[(y - 1) * GRID_WIDTH + x]);              /* top */
        case 5: return (uint8_t)(y + 1 < GRID_HEIGHT && grid[(y + 1) * GRID_WIDTH + x]);/* bottom */
        default: return 0;
    }
}

static inline void cfx_draw_cached_grid_cell_face_direct(DEFAULT_INT x, DEFAULT_INT y,
                                                         DEFAULT_INT tetromino_type,
                                                         DEFAULT_INT face_index)
{
    DEFAULT_INT cache_y = y - CFX_FACE_CACHE_Y_MIN;
    if ((uint16_t)x >= GRID_WIDTH || (uint16_t)cache_y >= CFX_FACE_CACHE_ROWS) {
        return;
    }

    CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][x][face_index];
    if (!cached->visible) {
        return;
    }
    if (cfx_grid_face_neighbor_occluded_fast(face_index, x, y)) {
        return;
    }

#if PLATFORM == NECPCFX
    cfx_renderer3d_draw_quad_offscreen_direct(&renderer3d,
        &cached->projected_vertices[0],
        &cached->projected_vertices[1],
        &cached->projected_vertices[2],
        &cached->projected_vertices[3],
        tetromino_type);
#else
    cfx_renderer3d_draw_quad(&renderer3d,
        &cached->projected_vertices[0],
        &cached->projected_vertices[1],
        &cached->projected_vertices[2],
        &cached->projected_vertices[3],
        tetromino_type);
#endif
}

static void cfx_draw_grid_cached_ordered_fast(void)
{
    /* Draw side/top/bottom-ish faces first, then front faces last.  The order is
       unchanged, but this hot path now uses direct grid-neighbor tests instead
       of the generic callback-driven visibility test. */
    static const uint8_t face_order[] = {2, 3, 4, 5, 0};

    for (DEFAULT_INT oi = 0; oi < (DEFAULT_INT)(sizeof(face_order) / sizeof(face_order[0])); ++oi) {
        DEFAULT_INT face_index = face_order[oi];
        for (DEFAULT_INT y = 0; y < GRID_HEIGHT; ++y) {
            const DEFAULT_INT row = y * GRID_WIDTH;
            for (DEFAULT_INT x = 0; x < GRID_WIDTH; ++x) {
                DEFAULT_INT cell_value = grid[row + x];
                if (cell_value) {
                    cfx_draw_cached_grid_cell_face_direct(x, y,
                                                          (DEFAULT_INT)(cell_value - 1),
                                                          face_index);
                }
            }
        }
    }
}

static CfxRenderDirty cfx_board_cache_full_projected_rect(void)
{
    CfxRenderDirty r = {SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0};
    cfx_precompute_cell_faces();

    for (DEFAULT_INT y = CFX_FACE_CACHE_Y_MIN; y < GRID_HEIGHT; ++y) {
        for (DEFAULT_INT x = 0; x < GRID_WIDTH; ++x) {
            DEFAULT_INT cache_y = y - CFX_FACE_CACHE_Y_MIN;
            for (DEFAULT_INT m = 0; m < 6; ++m) {
                CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][x][m];
                if (!cached->visible) {
                    continue;
                }
                for (DEFAULT_INT n = 0; n < 4; ++n) {
                    DEFAULT_INT px = cached->projected_vertices[n].x;
                    DEFAULT_INT py = cached->projected_vertices[n].y;
                    if (!r.active) {
                        r.x0 = r.x1 = px;
                        r.y0 = r.y1 = py;
                        r.active = 1;
                    } else {
                        if (px < r.x0) r.x0 = px;
                        if (py < r.y0) r.y0 = py;
                        if (px > r.x1) r.x1 = px;
                        if (py > r.y1) r.y1 = py;
                    }
                }
            }
        }
    }

    if (r.active) {
        r.x0 -= 8;
        r.y0 -= 8;
        r.x1 += 8;
        r.y1 += 8;
        cfx_rect_clamp(&r);
        cfx_render_dirty_align_x16(&r);
    }
    return r;
}

static inline void process_cube_block_faces(Point3D *transformed_vertices, DEFAULT_INT tetromino_type) {
    for (DEFAULT_INT m = 0; m < 6; m++) {
        //if (face_count >= MAX_FACES) break;

        DEFAULT_INT face_indices[4];
        for (DEFAULT_INT n = 0; n < 4; n++) {
            face_indices[n] = cube_faces_template[m * 4 + n];
        }

        Point3D *v0 = &transformed_vertices[face_indices[0]];
        Point3D *v1 = &transformed_vertices[face_indices[1]];
        Point3D *v2 = &transformed_vertices[face_indices[2]];

        // Compute face normal
        int32_t ax = v1->x - v0->x;
        int32_t ay = v1->y - v0->y;
		//int32_t az = v1->z - v0->z;

        int32_t bx = v2->x - v0->x;
        int32_t by = v2->y - v0->y;
        //int32_t bz = v2->z - v0->z;

        //int32_t nx = ay * bz - az * by;
        //int32_t ny = az * bx - ax * bz;
        int32_t nz = cfx_mul_i32(ax , by) - cfx_mul_i32(ay , bx);

        if (nz > 0) continue; // Back-face culling

        // Prepare projected vertices with per-face texture coordinates
        Point2D projected_points[4];
        DEFAULT_INT tex_coord_offset = m * 4 * 2;

        for (DEFAULT_INT n = 0; n < 4; n++) {
            Point3D *v = &transformed_vertices[face_indices[n]];
            int32_t u = cube_texcoords_template[tex_coord_offset + n * 2 + 0];
            int32_t v_tex = cube_texcoords_template[tex_coord_offset + n * 2 + 1];

            Point2D p = project(*v, u, v_tex);
            p.x += SCREEN_WIDTH_HALF;
            p.y += SCREEN_HEIGHT_HALF;

            projected_points[n] = p;
        }

        // Add face to the face list
        FaceToDraw *face_to_draw = face_list + face_count++;
        face_to_draw->projected_vertices[0] = projected_points[0];
        face_to_draw->projected_vertices[1] = projected_points[1];
        face_to_draw->projected_vertices[2] = projected_points[2];
        face_to_draw->projected_vertices[3] = projected_points[3];
        cfx_render_dirty_add_quad(projected_points);

        face_to_draw->average_depth = (v0->z + v1->z + v2->z + transformed_vertices[face_indices[3]].z) / 4;
        face_to_draw->tetromino_type = tetromino_type;
    }
}

static DEFAULT_INT cfx_collect_piece_cells(DEFAULT_INT type, DEFAULT_INT rotation, CfxPieceCell *cells)
{
    DEFAULT_INT count = 0;
    for (DEFAULT_INT i = 0; i < 4; ++i) {
        for (DEFAULT_INT j = 0; j < 4; ++j) {
            DEFAULT_INT index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
            if (puzzle_pieces[index] && count < 4) {
                cells[count].x = j;
                cells[count].y = i;
                count++;
            }
        }
    }
    return count;
}

static uint8_t cfx_piece_rotations_differ(DEFAULT_INT type, DEFAULT_INT a, DEFAULT_INT b)
{
    CfxPieceCell ca[4];
    CfxPieceCell cb[4];
    DEFAULT_INT na = cfx_collect_piece_cells(type, a, ca);
    DEFAULT_INT nb = cfx_collect_piece_cells(type, b, cb);
    if (na != nb) {
        return 1;
    }
    for (DEFAULT_INT i = 0; i < na; ++i) {
        uint8_t found = 0;
        for (DEFAULT_INT j = 0; j < nb; ++j) {
            if (ca[i].x == cb[j].x && ca[i].y == cb[j].y) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return 1;
        }
    }
    return 0;
}

static int32_t cfx_piece_origin_world_x256(DEFAULT_INT grid_x)
{
    return (int32_t)(((grid_x * CUBE_SIZE) - ((GRID_WIDTH * CUBE_SIZE) / 2)) << 8);
}

static int32_t cfx_piece_origin_world_y256(DEFAULT_INT grid_y)
{
    return (int32_t)((((GRID_HEIGHT - (grid_y + 1)) * CUBE_SIZE) - ((GRID_HEIGHT * CUBE_SIZE) / 2)) << 8);
}

static void cfx_move_anim_clear(void)
{
    cfx_move_anim.active = 0;
}

static DEFAULT_INT cfx_anim_linear_phase256(int32_t start_ms, int32_t duration_ms)
{
    int32_t elapsed = cfx_now_ms - start_ms;
    DEFAULT_INT phase;
    if (elapsed < 0) elapsed = 0;
    if (elapsed >= duration_ms) return 256;
    phase = (DEFAULT_INT)((elapsed * 256) / duration_ms);
    if (phase < 0) phase = 0;
    if (phase > 256) phase = 256;
    return phase;
}

static uint8_t cfx_move_anim_current_origin256(int32_t *world_x256, int32_t *world_y256)
{
    if (!cfx_move_anim.active ||
        cfx_move_anim.type != current_piece.type ||
        cfx_move_anim.rotation != current_piece.rotation) {
        cfx_move_anim.active = 0;
        *world_x256 = cfx_piece_origin_world_x256(current_piece.x);
        *world_y256 = cfx_piece_origin_world_y256(current_piece.y);
        return 0;
    }

    if (cfx_move_anim.to_x != current_piece.x || cfx_move_anim.to_y != current_piece.y) {
        int32_t dx = cfx_piece_origin_world_x256(current_piece.x) - cfx_piece_origin_world_x256(cfx_move_anim.to_x);
        int32_t dy = cfx_piece_origin_world_y256(current_piece.y) - cfx_piece_origin_world_y256(cfx_move_anim.to_y);
        cfx_move_anim.from_world_x256 += dx;
        cfx_move_anim.from_world_y256 += dy;
        cfx_move_anim.to_world_x256 += dx;
        cfx_move_anim.to_world_y256 += dy;
        cfx_move_anim.from_x += (DEFAULT_INT)(current_piece.x - cfx_move_anim.to_x);
        cfx_move_anim.from_y += (DEFAULT_INT)(current_piece.y - cfx_move_anim.to_y);
        cfx_move_anim.to_x = current_piece.x;
        cfx_move_anim.to_y = current_piece.y;
    }

    DEFAULT_INT phase = cfx_anim_linear_phase256(cfx_move_anim.start_ms, CFX_MOVE_ANIM_DURATION_MS);
    if (phase >= 256) {
        cfx_move_anim.active = 0;
        *world_x256 = cfx_piece_origin_world_x256(current_piece.x);
        *world_y256 = cfx_piece_origin_world_y256(current_piece.y);
        return 0;
    }

    *world_x256 = cfx_move_anim.from_world_x256 + (((cfx_move_anim.to_world_x256 - cfx_move_anim.from_world_x256) * phase + 128) >> 8);
    *world_y256 = cfx_move_anim.from_world_y256 + (((cfx_move_anim.to_world_y256 - cfx_move_anim.from_world_y256) * phase + 128) >> 8);
    return 1;
}

static uint8_t cfx_move_anim_current_offset(DEFAULT_INT *dx, DEFAULT_INT *dy)
{
    int32_t world_x256;
    int32_t world_y256;
    uint8_t active = cfx_move_anim_current_origin256(&world_x256, &world_y256);
    *dx = (DEFAULT_INT)((world_x256 - cfx_piece_origin_world_x256(current_piece.x)) >> 8);
    *dy = (DEFAULT_INT)((world_y256 - cfx_piece_origin_world_y256(current_piece.y)) >> 8);
    return active;
}

static void cfx_move_anim_start_from_origin(int32_t from_world_x256, int32_t from_world_y256,
                                            DEFAULT_INT from_x, DEFAULT_INT from_y,
                                            DEFAULT_INT to_x, DEFAULT_INT to_y,
                                            DEFAULT_INT rotation)
{
    if (from_x == to_x && from_y == to_y &&
        from_world_x256 == cfx_piece_origin_world_x256(to_x) &&
        from_world_y256 == cfx_piece_origin_world_y256(to_y)) {
        cfx_move_anim_clear();
        return;
    }

    cfx_move_anim.active = 1;
    cfx_move_anim.type = current_piece.type;
    cfx_move_anim.rotation = rotation;
    cfx_move_anim.from_x = from_x;
    cfx_move_anim.from_y = from_y;
    cfx_move_anim.to_x = to_x;
    cfx_move_anim.to_y = to_y;
    cfx_move_anim.from_world_x256 = from_world_x256;
    cfx_move_anim.from_world_y256 = from_world_y256;
    cfx_move_anim.to_world_x256 = cfx_piece_origin_world_x256(to_x);
    cfx_move_anim.to_world_y256 = cfx_piece_origin_world_y256(to_y);
    cfx_move_anim.start_ms = cfx_now_ms - CFX_MOVE_ANIM_INITIAL_LEAD_MS;
}

static void cfx_rotation_anim_clear(void)
{
    cfx_rotation_anim.active = 0;
}

static uint8_t cfx_rotation_cells_match_sign(const CfxPieceCell *from_cells,
                                             DEFAULT_INT from_count,
                                             const CfxPieceCell *to_cells,
                                             DEFAULT_INT to_count,
                                             DEFAULT_INT sign)
{
    DEFAULT_INT from_sum_x = 0;
    DEFAULT_INT from_sum_y = 0;
    DEFAULT_INT to_sum_x = 0;
    DEFAULT_INT to_sum_y = 0;

    if (from_count <= 0 || from_count != to_count) {
        return 0;
    }

    for (DEFAULT_INT i = 0; i < from_count; ++i) {
        from_sum_x += from_cells[i].x;
        from_sum_y += from_cells[i].y;
        to_sum_x += to_cells[i].x;
        to_sum_y += to_cells[i].y;
    }

    for (DEFAULT_INT i = 0; i < from_count; ++i) {
        DEFAULT_INT dx4 = (DEFAULT_INT)(from_cells[i].x * from_count - from_sum_x);
        DEFAULT_INT dy4 = (DEFAULT_INT)(from_cells[i].y * from_count - from_sum_y);
        DEFAULT_INT rx4;
        DEFAULT_INT ry4;
        uint8_t found = 0;

        /* Coordinates here are logical grid coordinates with Y increasing
           downward.  sign < 0 is the clockwise table advance used by most
           pieces; sign > 0 covers the mirrored L table and symmetric pieces. */
        if (sign < 0) {
            rx4 = (DEFAULT_INT)( dy4 + to_sum_x);
            ry4 = (DEFAULT_INT)(-dx4 + to_sum_y);
        } else {
            rx4 = (DEFAULT_INT)(-dy4 + to_sum_x);
            ry4 = (DEFAULT_INT)( dx4 + to_sum_y);
        }

        for (DEFAULT_INT j = 0; j < to_count; ++j) {
            if (rx4 == to_cells[j].x * to_count &&
                ry4 == to_cells[j].y * to_count) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }

    return 1;
}

static DEFAULT_INT cfx_rotation_anim_choose_sign(DEFAULT_INT type,
                                                 DEFAULT_INT from_rotation,
                                                 DEFAULT_INT to_rotation)
{
    CfxPieceCell from_cells[4];
    CfxPieceCell to_cells[4];
    DEFAULT_INT from_count = cfx_collect_piece_cells(type, from_rotation, from_cells);
    DEFAULT_INT to_count = cfx_collect_piece_cells(type, to_rotation, to_cells);
    DEFAULT_INT delta = (DEFAULT_INT)((to_rotation - from_rotation) & 3);
    DEFAULT_INT preferred = (delta == 3) ? 1 : -1;

    if (delta == 0 || delta == 2) {
        return preferred;
    }
    if (cfx_rotation_cells_match_sign(from_cells, from_count, to_cells, to_count, preferred)) {
        return preferred;
    }
    if (cfx_rotation_cells_match_sign(from_cells, from_count, to_cells, to_count, (DEFAULT_INT)-preferred)) {
        return (DEFAULT_INT)-preferred;
    }
    return preferred;
}

static void cfx_rotation_anim_start(DEFAULT_INT from_x, DEFAULT_INT from_y, DEFAULT_INT from_rotation,
                                    DEFAULT_INT to_x, DEFAULT_INT to_y, DEFAULT_INT to_rotation)
{
    if (from_rotation == to_rotation ||
        !cfx_piece_rotations_differ(current_piece.type, from_rotation, to_rotation)) {
        cfx_rotation_anim_clear();
        return;
    }

    cfx_move_anim_clear();
    cfx_rotation_anim.active = 1;
    cfx_rotation_anim.type = current_piece.type;
    cfx_rotation_anim.from_rotation = from_rotation;
    cfx_rotation_anim.to_rotation = to_rotation;
    cfx_rotation_anim.from_x = from_x;
    cfx_rotation_anim.from_y = from_y;
    cfx_rotation_anim.to_x = to_x;
    cfx_rotation_anim.to_y = to_y;
    cfx_rotation_anim.angle_sign = cfx_rotation_anim_choose_sign(current_piece.type, from_rotation, to_rotation);
    cfx_rotation_anim.start_ms = cfx_now_ms - CFX_ROTATION_ANIM_INITIAL_LEAD_MS;
}

static uint8_t cfx_rotation_anim_phase(DEFAULT_INT *phase256)
{
    if (!cfx_rotation_anim.active || cfx_rotation_anim.type != current_piece.type ||
        cfx_rotation_anim.to_rotation != current_piece.rotation) {
        cfx_rotation_anim.active = 0;
        *phase256 = 256;
        return 0;
    }

    if (cfx_rotation_anim.to_x != current_piece.x ||
        cfx_rotation_anim.to_y != current_piece.y) {
        DEFAULT_INT dx = (DEFAULT_INT)(current_piece.x - cfx_rotation_anim.to_x);
        DEFAULT_INT dy = (DEFAULT_INT)(current_piece.y - cfx_rotation_anim.to_y);
        cfx_rotation_anim.from_x = (DEFAULT_INT)(cfx_rotation_anim.from_x + dx);
        cfx_rotation_anim.from_y = (DEFAULT_INT)(cfx_rotation_anim.from_y + dy);
        cfx_rotation_anim.to_x = current_piece.x;
        cfx_rotation_anim.to_y = current_piece.y;
    }

    int32_t elapsed = cfx_now_ms - cfx_rotation_anim.start_ms;
    if (elapsed < 0) {
        elapsed = 0;
    }
    if (elapsed >= CFX_ROTATION_ANIM_DURATION_MS) {
        cfx_rotation_anim.active = 0;
        *phase256 = 256;
        return 0;
    }

    DEFAULT_INT t = (DEFAULT_INT)((elapsed * 256) / CFX_ROTATION_ANIM_DURATION_MS);
    if (t < 0) t = 0;
    if (t > 256) t = 256;

    /* Fast ease-out response.  Smoothstep had zero initial velocity and a long
       168 ms duration, so the first visible change could read as input lag. */
    {
        DEFAULT_INT inv = (DEFAULT_INT)(256 - t);
        *phase256 = (DEFAULT_INT)(256 - ((inv * inv + 128) >> 8));
    }
    if (*phase256 < 0) *phase256 = 0;
    if (*phase256 > 256) *phase256 = 256;
    return 1;
}

static inline DEFAULT_INT cfx_lerp_i32(DEFAULT_INT a, DEFAULT_INT b, DEFAULT_INT phase256)
{
    return (DEFAULT_INT)(a + (((b - a) * phase256 + 128) >> 8));
}

static inline void transform_cube_block_rotated_z(Point3D *transformed_vertices,
                                                  DEFAULT_INT x_offset, DEFAULT_INT y_offset, DEFAULT_INT z_offset,
                                                  DEFAULT_INT angle_z)
{
    for (DEFAULT_INT k = 0; k < 8; k++) {
        Point3D v = cube_vertices_template[k];
        v = rotateZ(v, angle_z);
        v.x += x_offset;
        v.y += y_offset;
        v.z += z_offset;
        transformed_vertices[k] = v;
    }
}

static DEFAULT_INT cfx_rotation_anim_angle(DEFAULT_INT phase256)
{
    DEFAULT_INT quarter = (DEFAULT_INT)((phase256 * (ANGLE_MAX / 4) + 128) >> 8);
    return (cfx_rotation_anim.angle_sign * quarter) & ANGLE_MASK;
}

static void cfx_draw_rotation_anim_piece(DEFAULT_INT phase256)
{
    CfxPieceCell from_cells[4];
    CfxPieceCell to_cells[4];
    DEFAULT_INT from_count = cfx_collect_piece_cells(current_piece.type, cfx_rotation_anim.from_rotation, from_cells);
    DEFAULT_INT to_count = cfx_collect_piece_cells(current_piece.type, cfx_rotation_anim.to_rotation, to_cells);
    if (from_count <= 0 || to_count <= 0 || from_count != to_count) {
        return;
    }

    DEFAULT_INT from_cx = 0;
    DEFAULT_INT from_cy = 0;
    DEFAULT_INT to_cx = 0;
    DEFAULT_INT to_cy = 0;

    for (DEFAULT_INT i = 0; i < from_count; ++i) {
        from_cx += (DEFAULT_INT)((cfx_rotation_anim.from_x + from_cells[i].x) * CUBE_SIZE - (GRID_WIDTH * CUBE_SIZE) / 2);
        from_cy += (DEFAULT_INT)((GRID_HEIGHT - (cfx_rotation_anim.from_y + from_cells[i].y + 1)) * CUBE_SIZE - (GRID_HEIGHT * CUBE_SIZE) / 2);
        to_cx += (DEFAULT_INT)((cfx_rotation_anim.to_x + to_cells[i].x) * CUBE_SIZE - (GRID_WIDTH * CUBE_SIZE) / 2);
        to_cy += (DEFAULT_INT)((GRID_HEIGHT - (cfx_rotation_anim.to_y + to_cells[i].y + 1)) * CUBE_SIZE - (GRID_HEIGHT * CUBE_SIZE) / 2);
    }

    from_cx /= from_count;
    from_cy /= from_count;
    to_cx /= to_count;
    to_cy /= to_count;

    DEFAULT_INT anim_cx = cfx_lerp_i32(from_cx, to_cx, phase256);
    DEFAULT_INT anim_cy = cfx_lerp_i32(from_cy, to_cy, phase256);
    DEFAULT_INT angle_z = cfx_rotation_anim_angle(phase256);
    int16_t sinA = sin_lookup[angle_z & ANGLE_MASK];
    int16_t cosA = cos_lookup[angle_z & ANGLE_MASK];

    for (DEFAULT_INT i = 0; i < from_count; ++i) {
        DEFAULT_INT sx = (DEFAULT_INT)((cfx_rotation_anim.from_x + from_cells[i].x) * CUBE_SIZE - (GRID_WIDTH * CUBE_SIZE) / 2);
        DEFAULT_INT sy = (DEFAULT_INT)((GRID_HEIGHT - (cfx_rotation_anim.from_y + from_cells[i].y + 1)) * CUBE_SIZE - (GRID_HEIGHT * CUBE_SIZE) / 2);
        DEFAULT_INT dx = (DEFAULT_INT)(sx - from_cx);
        DEFAULT_INT dy = (DEFAULT_INT)(sy - from_cy);
        DEFAULT_INT rdx = FIXED_TO_INT(cfx_mul_i32(dx, cosA) - cfx_mul_i32(dy, sinA));
        DEFAULT_INT rdy = FIXED_TO_INT(cfx_mul_i32(dx, sinA) + cfx_mul_i32(dy, cosA));
        DEFAULT_INT x_offset = (DEFAULT_INT)(anim_cx + rdx);
        DEFAULT_INT y_offset = (DEFAULT_INT)(anim_cy + rdy);
        Point3D transformed_vertices[8];

        /* Rigid-piece rotation: every cube shares the same angular transform
           around the piece centroid.  There is no per-cube pairing or per-cube
           landing correction, so the piece reads as one object instead of four
           independent blocks sliding into their destination grid cells. */
        transform_cube_block_rotated_z(transformed_vertices, x_offset, y_offset, STARTING_Z_OFFSET, angle_z);
        process_cube_block_faces(transformed_vertices, current_piece.type);
    }
}

void draw_current_piece() {
    DEFAULT_INT phase256 = 256;
    uint8_t animating = cfx_rotation_anim_phase(&phase256);

    // Store current state for next frame's undraw
    previous_piece_state.x = current_piece.x;
    previous_piece_state.y = current_piece.y;
    previous_piece_state.type = current_piece.type;
    previous_piece_state.rotation = current_piece.rotation;

    if (animating && phase256 < 256) {
        cfx_draw_rotation_anim_piece(phase256);
    } else {
        DEFAULT_INT type = current_piece.type;
        DEFAULT_INT rotation = current_piece.rotation;
        DEFAULT_INT grid_x = current_piece.x;
        DEFAULT_INT grid_y = current_piece.y;
        DEFAULT_INT visual_dx = 0;
        DEFAULT_INT visual_dy = 0;
        uint8_t moving = cfx_move_anim_current_offset(&visual_dx, &visual_dy);
        const int32_t *piece_shape = puzzle_pieces;

        for (DEFAULT_INT i = 0; i < 4; i++) {
            for (DEFAULT_INT j = 0; j < 4; j++) {
                DEFAULT_INT index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
                if (piece_shape[index]) {
                    if (moving) {
                        Point3D transformed_vertices[8];
                        DEFAULT_INT x_offset = (DEFAULT_INT)((grid_x + j) * CUBE_SIZE - (GRID_WIDTH * CUBE_SIZE) / 2 + visual_dx);
                        DEFAULT_INT y_offset = (DEFAULT_INT)((GRID_HEIGHT - (grid_y + i + 1)) * CUBE_SIZE - (GRID_HEIGHT * CUBE_SIZE) / 2 + visual_dy);
                        transform_cube_block(transformed_vertices, x_offset, y_offset, STARTING_Z_OFFSET);
                        process_cube_block_faces(transformed_vertices, type);
                    } else {
                        cfx_emit_cached_cell_faces((DEFAULT_INT)(grid_x + j),
                                                   (DEFAULT_INT)(grid_y + i),
                                                   type,
                                                   cfx_current_piece_cell_occupied);
                    }
                }
            }
        }
    }

    cfx_prev_active_draw_rect = cfx_active_render_dirty_rect(current_piece.type,
                                                            current_piece.rotation,
                                                            current_piece.x,
                                                            current_piece.y);
}

static CfxRenderDirty cfx_cell_sprite_rect(DEFAULT_INT x, DEFAULT_INT y, DEFAULT_INT margin)
{
    CfxRenderDirty r = {SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0};
    DEFAULT_INT cache_y = y - CFX_FACE_CACHE_Y_MIN;
    if ((uint16_t)x >= GRID_WIDTH || (uint16_t)cache_y >= CFX_FACE_CACHE_ROWS) {
        return r;
    }

    /* This is deliberately a 2D sprite-space bound.  It reads the precomputed
       projected cell silhouette but does not submit locked geometry, compare
       face depths, or rerun the 3D board renderer while a piece moves. */
    for (DEFAULT_INT m = 0; m < 6; ++m) {
        CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][x][m];
        if (!cached->visible) {
            continue;
        }

        for (DEFAULT_INT n = 0; n < 4; ++n) {
            DEFAULT_INT px = cached->projected_vertices[n].x;
            DEFAULT_INT py = cached->projected_vertices[n].y;
            if (!r.active) {
                r.x0 = r.x1 = px;
                r.y0 = r.y1 = py;
                r.active = 1;
            } else {
                if (px < r.x0) r.x0 = px;
                if (py < r.y0) r.y0 = py;
                if (px > r.x1) r.x1 = px;
                if (py > r.y1) r.y1 = py;
            }
        }
    }

    if (r.active) {
        r.x0 -= margin;
        r.y0 -= margin;
        r.x1 += margin + 1;
        r.y1 += margin + 1;
        if (r.x0 < 0) r.x0 = 0;
        if (r.y0 < 0) r.y0 = 0;
        if (r.x1 > SCREEN_WIDTH) r.x1 = SCREEN_WIDTH;
        if (r.y1 > SCREEN_HEIGHT) r.y1 = SCREEN_HEIGHT;
        if (r.x0 >= r.x1 || r.y0 >= r.y1) {
            r.active = 0;
        }
    }

    return r;
}

static void cfx_collect_active_shadow_shape(CfxActiveShadowShape *shape,
                                            DEFAULT_INT type,
                                            DEFAULT_INT rotation,
                                            DEFAULT_INT grid_x,
                                            DEFAULT_INT grid_y)
{
    const int32_t *piece_shape = puzzle_pieces;

    shape->count = 0;
    shape->outer.x0 = 0;
    shape->outer.y0 = 0;
    shape->outer.x1 = 0;
    shape->outer.y1 = 0;
    shape->outer.active = 0;

    for (DEFAULT_INT i = 0; i < 4; ++i) {
        for (DEFAULT_INT j = 0; j < 4; ++j) {
            DEFAULT_INT index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
            if (!piece_shape[index] || shape->count >= CFX_ACTIVE_SHADOW_MAX_RECTS) {
                continue;
            }

            CfxRenderDirty cell_rect = cfx_cell_sprite_rect(grid_x + j,
                                                            grid_y + i,
                                                            CFX_ACTIVE_SHADOW_MARGIN);
            if (!cell_rect.active) {
                continue;
            }

            cell_rect = cfx_shift_rect(cell_rect, CFX_ACTIVE_SHADOW_DX, CFX_ACTIVE_SHADOW_DY);
            if (!cell_rect.active) {
                continue;
            }

            /* Opaque part: a slightly inflated copy of the visible cell
               silhouette.  The inflation is symmetric; landing/contact frames
               use the same mask as all other frames. */
            cell_rect.x0 -= CFX_ACTIVE_SHADOW_CORE_INFLATE;
            cell_rect.y0 -= CFX_ACTIVE_SHADOW_CORE_INFLATE;
            cell_rect.x1 += CFX_ACTIVE_SHADOW_CORE_INFLATE;
            cell_rect.y1 += CFX_ACTIVE_SHADOW_CORE_INFLATE;
            cell_rect.active = 1;
            cfx_rect_clamp(&cell_rect);
            if (!cell_rect.active) {
                continue;
            }

            CfxRenderDirty outer = cell_rect;
            outer.x0 -= CFX_ACTIVE_SHADOW_HALO;
            outer.y0 -= CFX_ACTIVE_SHADOW_HALO;
            outer.x1 += CFX_ACTIVE_SHADOW_HALO;
            outer.y1 += CFX_ACTIVE_SHADOW_HALO;
            outer.active = 1;
            cfx_rect_clamp(&outer);

            shape->rects[shape->count++] = cell_rect;
            if (outer.active) {
                cfx_render_dirty_union(&shape->outer, &outer);
            }
        }
    }
}

static void cfx_draw_active_piece_field(void)
{
    CfxActiveShadowShape shape;
    uint8_t phase = (uint8_t)((uint32_t)cfx_now_ms >> 7);

    if (cfx_rotation_anim.active || cfx_move_anim.active) {
        /* During short visual interpolation, the old grid-aligned black
           foreground field reads as an input-lagged square shadow.  The dirty
           rect already restores the board underneath the interpolated 3D
           piece, so suppress the grid shadow until the piece has settled. */
        cfx_shadow_snapshot_clear();
        return;
    }

    cfx_collect_active_shadow_shape(&shape,
                                    current_piece.type,
                                    current_piece.rotation,
                                    current_piece.x,
                                    current_piece.y);

    if (shape.outer.active) {
        cfx_draw_active_shadow_shape(&shape, phase);
        cfx_shadow_snapshot_begin(&shape.outer);
        cfx_render_dirty_add_rect(shape.outer.x0, shape.outer.y0, shape.outer.x1, shape.outer.y1);
    } else {
        cfx_shadow_snapshot_clear();
    }
}

static void cfx_draw_active_piece_field_cache_repair(void)
{
#if PLATFORM == NECPCFX
    /* The PC-FX foreground is drawn directly to KING KRAM.  This repair pass
       relies on the CPU staging framebuffer to identify pixels changed by the
       3D renderer; without staging writes it can upload stale shadow/zero data
       over textured blocks.  Skip it on PC-FX and let the direct KING 3D pass
       remain the last writer for the active piece. */
    cfx_shadow_snapshot_clear();
    return;
#endif
    CfxActiveShadowShape shape;
    uint8_t phase = (uint8_t)((uint32_t)cfx_now_ms >> 7);

    cfx_collect_active_shadow_shape(&shape,
                                    current_piece.type,
                                    current_piece.rotation,
                                    current_piece.x,
                                    current_piece.y);

    if (!shape.outer.active || shape.count <= 0 || !cfx_shadow_pre_piece_rect.active) {
        cfx_shadow_snapshot_clear();
        return;
    }

    /* Final shadow repair for the cached-board path.

       This is the part that fixes the contact-frame holes.  The pre-piece pass
       draws the complete shadow over the cached board.  After the active piece
       is drawn, repair the complete shadow again, but only on pixels that are
       byte-for-byte unchanged from the pre-piece snapshot.  Changed pixels are
       pixels touched by the active piece renderer, so they are left alone.

       This is still a 2D sprite-space operation: no locked-piece 3D redraw, no
       face depth sorting, and no board-cache/background test that can erase the
       shadow near adjacent blocks. */
    for (DEFAULT_INT y = shape.outer.y0; y < shape.outer.y1; ++y) {
        DEFAULT_INT index = y * SCREEN_WIDTH + shape.outer.x0;
        for (DEFAULT_INT x = shape.outer.x0; x < shape.outer.x1; ++x, ++index) {
            uint8_t color;
            if (cfx_shadow_snapshot_unchanged(x, y) &&
                cfx_active_shadow_pixel(&shape, x, y, phase, &color)) {
                GAME_FRAMEBUFFER[index] = color;
            }
        }
    }

    cfx_game_upload_rect_pixels((const uint8_t *)GAME_FRAMEBUFFER, shape.outer.x0, shape.outer.y0,
                                shape.outer.x1 - shape.outer.x0,
                                shape.outer.y1 - shape.outer.y0);
    cfx_render_dirty_add_rect(shape.outer.x0, shape.outer.y0, shape.outer.x1, shape.outer.y1);
    cfx_shadow_snapshot_clear();
}

static CfxRenderDirty cfx_active_shadow_dirty_rect(DEFAULT_INT type,
                                                   DEFAULT_INT rotation,
                                                   DEFAULT_INT grid_x,
                                                   DEFAULT_INT grid_y)
{
    CfxActiveShadowShape shape;
    cfx_collect_active_shadow_shape(&shape, type, rotation, grid_x, grid_y);
    return shape.outer;
}

static DEFAULT_INT cfx_current_piece_landing_y(void)
{
    DEFAULT_INT landing_y = current_piece.y;
    while (!check_collision(current_piece.x, (DEFAULT_INT)(landing_y + 1), current_piece.rotation)) {
        ++landing_y;
    }
    return landing_y;
}

static CfxRenderDirty cfx_landing_ghost_dirty_rect(DEFAULT_INT type,
                                                   DEFAULT_INT rotation,
                                                   DEFAULT_INT grid_x,
                                                   DEFAULT_INT grid_y)
{
    (void)type;
    (void)rotation;
    CfxActiveShadowShape shape;
    DEFAULT_INT landing_y = cfx_current_piece_landing_y();
    if (landing_y <= grid_y) {
        CfxRenderDirty empty = {0, 0, 0, 0, 0};
        return empty;
    }
    cfx_collect_active_shadow_shape(&shape, current_piece.type, current_piece.rotation, grid_x, landing_y);
    return shape.outer;
}

static void cfx_draw_landing_ghost_piece(void)
{
    DEFAULT_INT landing_y = cfx_current_piece_landing_y();
    if (landing_y <= current_piece.y) {
        cfx_prev_landing_dirty_rect.active = 0;
        return;
    }

    CfxActiveShadowShape shape;
    cfx_collect_active_shadow_shape(&shape,
                                    current_piece.type,
                                    current_piece.rotation,
                                    current_piece.x,
                                    landing_y);
    if (!shape.outer.active || shape.count <= 0) {
        cfx_prev_landing_dirty_rect.active = 0;
        return;
    }

    /* TGM-style landing accuracy marker.  This is intentionally a cheap 2D
       outline over the board cache, not another 3D polygon pass.  The active
       3D piece is drawn after this and remains authoritative. */
    const uint8_t ghost_color = 255;
    cfx_game_prepare_dirty_rect(shape.outer.x0, shape.outer.y0,
                                shape.outer.x1 - shape.outer.x0,
                                shape.outer.y1 - shape.outer.y0);

    for (DEFAULT_INT i = 0; i < shape.count; ++i) {
        CfxRenderDirty r = shape.rects[i];
        if (!r.active) continue;
        if (r.x0 < 0) r.x0 = 0;
        if (r.y0 < 0) r.y0 = 0;
        if (r.x1 > SCREEN_WIDTH) r.x1 = SCREEN_WIDTH;
        if (r.y1 > SCREEN_HEIGHT) r.y1 = SCREEN_HEIGHT;
        if (r.x1 - r.x0 < 2 || r.y1 - r.y0 < 2) continue;

        for (DEFAULT_INT x = r.x0; x < r.x1; ++x) {
            GAME_FRAMEBUFFER[r.y0 * SCREEN_WIDTH + x] = ghost_color;
            GAME_FRAMEBUFFER[(r.y1 - 1) * SCREEN_WIDTH + x] = ghost_color;
        }
        for (DEFAULT_INT y = r.y0 + 1; y < r.y1 - 1; ++y) {
            GAME_FRAMEBUFFER[y * SCREEN_WIDTH + r.x0] = ghost_color;
            GAME_FRAMEBUFFER[y * SCREEN_WIDTH + (r.x1 - 1)] = ghost_color;
        }
    }

    cfx_game_upload_rect_pixels((const uint8_t *)GAME_FRAMEBUFFER,
                                shape.outer.x0, shape.outer.y0,
                                shape.outer.x1 - shape.outer.x0,
                                shape.outer.y1 - shape.outer.y0);
    cfx_render_dirty_add_rect(shape.outer.x0, shape.outer.y0, shape.outer.x1, shape.outer.y1);
    cfx_prev_landing_dirty_rect = shape.outer;
}

static inline void compute_bounding_box(Point3D *transformed_vertices, DEFAULT_INT *min_x, DEFAULT_INT *min_y, DEFAULT_INT *max_x, DEFAULT_INT *max_y) {
    for (DEFAULT_INT k = 0; k < 8; k++) {
        Point2D projected = project(transformed_vertices[k], 0, 0);
        projected.x += SCREEN_WIDTH_HALF;
        projected.y += SCREEN_HEIGHT_HALF;

        if (projected.x < *min_x) *min_x = projected.x;
        if (projected.y < *min_y) *min_y = projected.y;
        if (projected.x > *max_x) *max_x = projected.x;
        if (projected.y > *max_y) *max_y = projected.y;
    }
}


static CfxRenderDirty cfx_piece_dirty_rect(DEFAULT_INT type, DEFAULT_INT rotation, DEFAULT_INT grid_x, DEFAULT_INT grid_y)
{
    CfxRenderDirty r = {SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0};
    const int32_t *piece_shape = puzzle_pieces;

    for (DEFAULT_INT i = 0; i < 4; i++) {
        for (DEFAULT_INT j = 0; j < 4; j++) {
            DEFAULT_INT index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
            if (!piece_shape[index]) {
                continue;
            }

            DEFAULT_INT x = grid_x + j;
            DEFAULT_INT y = grid_y + i;
            DEFAULT_INT cache_y = y - CFX_FACE_CACHE_Y_MIN;
            if ((uint16_t)x >= GRID_WIDTH || (uint16_t)cache_y >= CFX_FACE_CACHE_ROWS) {
                continue;
            }

            for (DEFAULT_INT m = 0; m < 6; ++m) {
                CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][x][m];
                if (!cached->visible) {
                    continue;
                }
                for (DEFAULT_INT n = 0; n < 4; ++n) {
                    DEFAULT_INT px = cached->projected_vertices[n].x;
                    DEFAULT_INT py = cached->projected_vertices[n].y;
                    if (!r.active) {
                        r.x0 = r.x1 = px;
                        r.y0 = r.y1 = py;
                        r.active = 1;
                    } else {
                        if (px < r.x0) r.x0 = px;
                        if (py < r.y0) r.y0 = py;
                        if (px > r.x1) r.x1 = px;
                        if (py > r.y1) r.y1 = py;
                    }
                }
            }
        }
    }

    {
        CfxRenderDirty shadow = cfx_active_shadow_dirty_rect(type, rotation, grid_x, grid_y);
        CfxRenderDirty ghost = cfx_landing_ghost_dirty_rect(type, rotation, grid_x, grid_y);
        cfx_render_dirty_union(&r, &shadow);
        cfx_render_dirty_union(&r, &ghost);
        cfx_render_dirty_union(&r, &cfx_prev_landing_dirty_rect);
    }

    if (r.active) {
        /* One extra pixel guards against dither phase changes on the fringe. */
        r.x0 -= 1;
        r.y0 -= 1;
        r.x1 += 1;
        r.y1 += 1;
        cfx_rect_clamp(&r);
    }

    return r;
}

static CfxRenderDirty cfx_active_render_dirty_rect(DEFAULT_INT type, DEFAULT_INT rotation, DEFAULT_INT grid_x, DEFAULT_INT grid_y)
{
    CfxRenderDirty r = cfx_piece_dirty_rect(type, rotation, grid_x, grid_y);

    if (cfx_rotation_anim.active && cfx_rotation_anim.type == type) {
        CfxRenderDirty from = cfx_piece_dirty_rect(type, cfx_rotation_anim.from_rotation, cfx_rotation_anim.from_x, cfx_rotation_anim.from_y);
        CfxRenderDirty to = cfx_piece_dirty_rect(type, cfx_rotation_anim.to_rotation, cfx_rotation_anim.to_x, cfx_rotation_anim.to_y);
        cfx_render_dirty_union(&r, &from);
        cfx_render_dirty_union(&r, &to);
    }

    if (cfx_move_anim.active && cfx_move_anim.type == type) {
        CfxRenderDirty from = cfx_piece_dirty_rect(type, cfx_move_anim.rotation, cfx_move_anim.from_x, cfx_move_anim.from_y);
        CfxRenderDirty to = cfx_piece_dirty_rect(type, cfx_move_anim.rotation, cfx_move_anim.to_x, cfx_move_anim.to_y);
        cfx_render_dirty_union(&r, &from);
        cfx_render_dirty_union(&r, &to);
    }

    if (r.active) {
        DEFAULT_INT margin = (cfx_rotation_anim.active && cfx_rotation_anim.type == type) ? CFX_ROTATION_ANIM_DIRTY_MARGIN :
                             ((cfx_move_anim.active && cfx_move_anim.type == type) ? CFX_MOVE_ANIM_DIRTY_MARGIN : 2);
        r.x0 -= margin;
        r.y0 -= margin;
        r.x1 += margin;
        r.y1 += margin;
        cfx_rect_clamp(&r);
    }
    return r;
}

void undraw_previous_piece() {
    CfxRenderDirty r = cfx_piece_dirty_rect(previous_piece_state.type,
                                           previous_piece_state.rotation,
                                           previous_piece_state.x,
                                           previous_piece_state.y);
    cfx_render_dirty_union(&r, &cfx_prev_active_draw_rect);
    if (!r.active) {
        return;
    }

    cfx_game_adjust_undraw_min_x(&r.x0);
    if (r.x0 < 0) r.x0 = 0;

    cfx_render_dirty_add_rect(r.x0, r.y0, r.x1, r.y1);
    cfx_restore_board_rect(&r);
}



void draw_grid() {
    for (DEFAULT_INT i = 0; i < GRID_HEIGHT; i++) {
        for (DEFAULT_INT j = 0; j < GRID_WIDTH; j++) {
            DEFAULT_INT cell_value = grid[i * GRID_WIDTH + j];
            if (cell_value) {
                DEFAULT_INT tetromino_type = cell_value - 1;
                cfx_emit_cached_cell_faces(j, i, tetromino_type, cfx_grid_cell_occupied);
            }
        }
    }
}

static CfxRenderDirty cfx_grid_region_dirty_rect(DEFAULT_INT gx0, DEFAULT_INT gy0, DEFAULT_INT gx1, DEFAULT_INT gy1)
{
    CfxRenderDirty r = {SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0};
    if (gx0 < 0) gx0 = 0;
    if (gy0 < 0) gy0 = 0;
    if (gx1 > GRID_WIDTH) gx1 = GRID_WIDTH;
    if (gy1 > GRID_HEIGHT) gy1 = GRID_HEIGHT;

    for (DEFAULT_INT y = gy0; y < gy1; ++y) {
        for (DEFAULT_INT x = gx0; x < gx1; ++x) {
            DEFAULT_INT cell_value = grid[y * GRID_WIDTH + x];
            if (!cell_value) continue;
            DEFAULT_INT cache_y = y - CFX_FACE_CACHE_Y_MIN;
            if ((uint16_t)x >= GRID_WIDTH || (uint16_t)cache_y >= CFX_FACE_CACHE_ROWS) continue;

            for (DEFAULT_INT m = 0; m < 6; ++m) {
                CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][x][m];
                if (!cached->visible) {
                    continue;
                }
                /* Dirty coverage must include faces that were visible before the
                   lock but become internal after the new cell is merged.  The
                   previous incremental cache pass computed this rect from only
                   the post-merge visible faces, so the clear/copy region could
                   omit old side/top pixels and leave apparent holes/stale cuts
                   until a later full rebuild.  Redraw still emits only the
                   currently visible faces; this wider rect only decides what
                   bitmap area is cleared to bg_game and re-uploaded to KING. */
                for (DEFAULT_INT n = 0; n < 4; ++n) {
                    DEFAULT_INT px = cached->projected_vertices[n].x;
                    DEFAULT_INT py = cached->projected_vertices[n].y;
                    if (!r.active) {
                        r.x0 = r.x1 = px;
                        r.y0 = r.y1 = py;
                        r.active = 1;
                    } else {
                        if (px < r.x0) r.x0 = px;
                        if (py < r.y0) r.y0 = py;
                        if (px > r.x1) r.x1 = px;
                        if (py > r.y1) r.y1 = py;
                    }
                }
            }
        }
    }

    if (r.active) {
        r.x0 -= 6;
        r.y0 -= 6;
        r.x1 += 6;
        r.y1 += 6;
        cfx_rect_clamp(&r);
    }
    return r;
}


static inline void cfx_dirty_include_cached_cell(CfxRenderDirty *r, DEFAULT_INT x, DEFAULT_INT y)
{
    DEFAULT_INT cache_y = y - CFX_FACE_CACHE_Y_MIN;
    if ((uint16_t)x >= GRID_WIDTH || (uint16_t)cache_y >= CFX_FACE_CACHE_ROWS) {
        return;
    }
    if (!grid[y * GRID_WIDTH + x]) {
        return;
    }

    for (DEFAULT_INT m = 0; m < 6; ++m) {
        CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][x][m];
        if (!cached->visible) {
            continue;
        }
        for (DEFAULT_INT n = 0; n < 4; ++n) {
            DEFAULT_INT px = cached->projected_vertices[n].x;
            DEFAULT_INT py = cached->projected_vertices[n].y;
            if (!r->active) {
                r->x0 = r->x1 = px;
                r->y0 = r->y1 = py;
                r->active = 1;
            } else {
                if (px < r->x0) r->x0 = px;
                if (py < r->y0) r->y0 = py;
                if (px > r->x1) r->x1 = px;
                if (py > r->y1) r->y1 = py;
            }
        }
    }
}

static CfxRenderDirty cfx_locked_piece_damage_rect(const Piece *locked)
{
    CfxRenderDirty r = {SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0};
    const int32_t *piece_shape = puzzle_pieces;
    static const int8_t dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

    for (DEFAULT_INT i = 0; i < 4; ++i) {
        for (DEFAULT_INT j = 0; j < 4; ++j) {
            DEFAULT_INT index = GET_PUZZLE_PIECE_INDEX(locked->type, locked->rotation, i, j);
            if (!piece_shape[index]) {
                continue;
            }
            DEFAULT_INT x = locked->x + j;
            DEFAULT_INT y = locked->y + i;
            if ((uint16_t)x >= GRID_WIDTH || (uint16_t)y >= GRID_HEIGHT) {
                continue;
            }

            /* New locked cell: draw it into the board cache. */
            cfx_dirty_include_cached_cell(&r, x, y);

            /* Adjacent locked cells can lose an exposed side/top/bottom face now
               that this cell exists.  Damage only those immediate neighbors
               instead of every occupied cell in the piece's bounding box. */
            for (DEFAULT_INT d = 0; d < 4; ++d) {
                DEFAULT_INT nx = (DEFAULT_INT)(x + dirs[d][0]);
                DEFAULT_INT ny = (DEFAULT_INT)(y + dirs[d][1]);
                if ((uint16_t)nx < GRID_WIDTH && (uint16_t)ny < GRID_HEIGHT) {
                    cfx_dirty_include_cached_cell(&r, nx, ny);
                }
            }
        }
    }

    if (r.active) {
        r.x0 -= 6;
        r.y0 -= 6;
        r.x1 += 6;
        r.y1 += 6;
        cfx_rect_clamp(&r);
    }
    return r;
}

static void cfx_emit_grid_region(DEFAULT_INT gx0, DEFAULT_INT gy0, DEFAULT_INT gx1, DEFAULT_INT gy1)
{
    if (gx0 < 0) gx0 = 0;
    if (gy0 < 0) gy0 = 0;
    if (gx1 > GRID_WIDTH) gx1 = GRID_WIDTH;
    if (gy1 > GRID_HEIGHT) gy1 = GRID_HEIGHT;

    for (DEFAULT_INT y = gy0; y < gy1; ++y) {
        for (DEFAULT_INT x = gx0; x < gx1; ++x) {
            DEFAULT_INT cell_value = grid[y * GRID_WIDTH + x];
            if (cell_value) {
                cfx_emit_cached_cell_faces(x, y, (DEFAULT_INT)(cell_value - 1), cfx_grid_cell_occupied);
            }
        }
    }
}

static inline uint8_t cfx_cached_face_intersects_rect(const CfxCachedCellFace *cached, const CfxRenderDirty *r)
{
    DEFAULT_INT x0 = SCREEN_WIDTH;
    DEFAULT_INT y0 = SCREEN_HEIGHT;
    DEFAULT_INT x1 = 0;
    DEFAULT_INT y1 = 0;

    for (DEFAULT_INT n = 0; n < 4; ++n) {
        DEFAULT_INT px = cached->projected_vertices[n].x;
        DEFAULT_INT py = cached->projected_vertices[n].y;
        if (px < x0) x0 = px;
        if (py < y0) y0 = py;
        if (px > x1) x1 = px;
        if (py > y1) y1 = py;
    }

    /* Inclusive quad bounds vs exclusive dirty rect bounds. */
    return (uint8_t)(x1 >= r->x0 && y1 >= r->y0 && x0 < r->x1 && y0 < r->y1);
}

static void cfx_emit_grid_faces_intersecting_rect(const CfxRenderDirty *r)
{
    if (!r->active) {
        return;
    }

    for (DEFAULT_INT y = 0; y < GRID_HEIGHT; ++y) {
        for (DEFAULT_INT x = 0; x < GRID_WIDTH; ++x) {
            DEFAULT_INT cell_value = grid[y * GRID_WIDTH + x];
            if (!cell_value) {
                continue;
            }

            DEFAULT_INT cache_y = y - CFX_FACE_CACHE_Y_MIN;
            if ((uint16_t)x >= GRID_WIDTH || (uint16_t)cache_y >= CFX_FACE_CACHE_ROWS) {
                continue;
            }

            for (DEFAULT_INT m = 0; m < 6; ++m) {
                CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][x][m];
                if (!cached->visible) {
                    continue;
                }
                if (cfx_face_neighbor_occluded(m, x, y, cfx_grid_cell_occupied)) {
                    continue;
                }
                if (!cfx_cached_face_intersects_rect(cached, r)) {
                    continue;
                }
                if (face_count >= MAX_FACES) {
                    return;
                }

                FaceToDraw *face_to_draw = face_list + face_count++;
                face_to_draw->projected_vertices[0] = cached->projected_vertices[0];
                face_to_draw->projected_vertices[1] = cached->projected_vertices[1];
                face_to_draw->projected_vertices[2] = cached->projected_vertices[2];
                face_to_draw->projected_vertices[3] = cached->projected_vertices[3];
                face_to_draw->average_depth = cached->average_depth;
                face_to_draw->tetromino_type = (DEFAULT_INT)(cell_value - 1);
                cfx_render_dirty_add_quad(face_to_draw->projected_vertices);
            }
        }
    }
}


static void cfx_draw_grid_cached_ordered_fast_intersecting_rect(const CfxRenderDirty *r)
{
    if (!r->active) {
        return;
    }

    /* Non-clearing piece locks only need to refresh a screen-space damage rect.
       The previous correct path still populated face_list, sorted it, and used
       the generic quad renderer.  On PC-FX this could turn a simple lock into a
       visible hitch on dense boards.  Use the same deterministic painter order
       and direct offscreen tile renderer as the fast line-clear rebuild, but
       gate every cached face against the damaged pixel rectangle. */
    static const uint8_t face_order[] = {2, 3, 4, 5, 0};

    for (DEFAULT_INT oi = 0; oi < (DEFAULT_INT)(sizeof(face_order) / sizeof(face_order[0])); ++oi) {
        DEFAULT_INT face_index = face_order[oi];
        for (DEFAULT_INT y = 0; y < GRID_HEIGHT; ++y) {
            const DEFAULT_INT row = y * GRID_WIDTH;
            for (DEFAULT_INT x = 0; x < GRID_WIDTH; ++x) {
                DEFAULT_INT cell_value = grid[row + x];
                if (!cell_value) {
                    continue;
                }

                DEFAULT_INT cache_y = y - CFX_FACE_CACHE_Y_MIN;
                if ((uint16_t)x >= GRID_WIDTH || (uint16_t)cache_y >= CFX_FACE_CACHE_ROWS) {
                    continue;
                }

                CfxCachedCellFace *cached = &cfx_cell_faces[cache_y][x][face_index];
                if (!cached->visible) {
                    continue;
                }
                if (cfx_grid_face_neighbor_occluded_fast(face_index, x, y)) {
                    continue;
                }
                if (!cfx_cached_face_intersects_rect(cached, r)) {
                    continue;
                }

                cfx_draw_cached_grid_cell_face_direct(x, y, (DEFAULT_INT)(cell_value - 1), face_index);
            }
        }
    }
}

static void cfx_board_cache_rebuild_grid_region_to_screen(DEFAULT_INT gx0, DEFAULT_INT gy0, DEFAULT_INT gx1, DEFAULT_INT gy1)
{
    if (!cfx_board_cache_valid || cfx_board_cache_rebuild_pending) {
        cfx_board_cache_rebuild_to_screen();
        return;
    }

    CfxRenderDirty r = cfx_grid_region_dirty_rect(gx0, gy0, gx1, gy1);
    if (!r.active) {
        return;
    }

    for (DEFAULT_INT y = r.y0; y < r.y1; ++y) {
        DEFAULT_INT off = y * SCREEN_WIDTH + r.x0;
        my_memcpy32(cfx_board_cache + off, bg_game + off, r.x1 - r.x0);
    }

    cfx_game_begin_offscreen_render();
    cfx_renderer3d_set_framebuffer(&renderer3d, cfx_board_cache);
    cfx_render_dirty_reset();
    face_count = 0;
    /* Correctness point: this dirty pixel rect is a screen-space rectangle,
       not a strict grid-space rectangle.  Due to the fixed isometric cube
       projection and depth ordering, faces from cells outside the placed
       piece's local grid neighborhood can overlap the pixels we just cleared.
       The old safe path emitted every intersecting face to face_list, sorted it,
       then ran the generic quad renderer.  That was correct but too slow on
       dense PC-FX placement frames.  Draw the same intersecting cached faces
       directly in the fixed locked-board painter order instead. */
    cfx_draw_grid_cached_ordered_fast_intersecting_rect(&r);
    face_count = 0;
    cfx_render_dirty_reset();
    cfx_renderer3d_set_framebuffer(&renderer3d, GAME_FRAMEBUFFER);
    cfx_game_end_offscreen_render();

    for (DEFAULT_INT y = r.y0; y < r.y1; ++y) {
        DEFAULT_INT off = y * SCREEN_WIDTH + r.x0;
        my_memcpy32(GAME_FRAMEBUFFER + off, cfx_board_cache + off, r.x1 - r.x0);
    }

#if PLATFORM == NECPCFX
    cfx_game_pcfx_set_board_restore_source((const uint8_t *)cfx_board_cache);
    cfx_game_pcfx_board_cache_rect_changed((const uint8_t *)cfx_board_cache,
                                           r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
#endif
    cfx_render_dirty_union(&cfx_board_cache_visible_rect, &r);
    cfx_render_dirty_add_rect(r.x0, r.y0, r.x1, r.y1);
}

static void cfx_board_cache_update_locked_piece_region(const Piece *locked)
{
    if (!cfx_board_cache_valid || cfx_board_cache_rebuild_pending) {
        cfx_board_cache_rebuild_to_screen();
        return;
    }

    CfxRenderDirty r = cfx_locked_piece_damage_rect(locked);
    if (!r.active) {
        cfx_board_cache_invalidate();
        return;
    }
    cfx_render_dirty_align_x16(&r);

    for (DEFAULT_INT y = r.y0; y < r.y1; ++y) {
        DEFAULT_INT off = y * SCREEN_WIDTH + r.x0;
        my_memcpy32(cfx_board_cache + off, bg_game + off, r.x1 - r.x0);
    }

    cfx_game_begin_offscreen_render();
    cfx_renderer3d_set_framebuffer(&renderer3d, cfx_board_cache);
    cfx_render_dirty_reset();
    face_count = 0;
    cfx_draw_grid_cached_ordered_fast_intersecting_rect(&r);
    face_count = 0;
    cfx_render_dirty_reset();
    cfx_renderer3d_set_framebuffer(&renderer3d, GAME_FRAMEBUFFER);
    cfx_game_end_offscreen_render();

    for (DEFAULT_INT y = r.y0; y < r.y1; ++y) {
        DEFAULT_INT off = y * SCREEN_WIDTH + r.x0;
        my_memcpy32(GAME_FRAMEBUFFER + off, cfx_board_cache + off, r.x1 - r.x0);
    }

#if PLATFORM == NECPCFX
    cfx_game_pcfx_set_board_restore_source((const uint8_t *)cfx_board_cache);
    cfx_game_pcfx_board_cache_rect_changed_deferred_page((const uint8_t *)cfx_board_cache,
                                                         r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
#endif
    cfx_render_dirty_union(&cfx_board_cache_visible_rect, &r);
    cfx_render_dirty_add_rect(r.x0, r.y0, r.x1, r.y1);
}


static void cfx_board_cache_rebuild_fast_ordered_to_screen(void)
{
    /* Line clears globally shift rows, but the old cache path still treated every
       clear as a worst-case full-grid foreground upload.  Track the previous and
       newly occupied projected board rectangles and only clear/copy/upload their
       union.  This still erases pixels from the cleared row/top-exposed area, but
       avoids pushing the empty title/background margins through KING on dense
       one-line clears. */
    CfxRenderDirty old_visible = cfx_board_cache_visible_rect;
    CfxRenderDirty new_visible = cfx_grid_region_dirty_rect(0, 0, GRID_WIDTH, GRID_HEIGHT);
    CfxRenderDirty upload = old_visible;
    cfx_render_dirty_union(&upload, &new_visible);
    cfx_render_dirty_align_x16(&upload);

    if (upload.active) {
        const DEFAULT_INT width = upload.x1 - upload.x0;
        for (DEFAULT_INT y = upload.y0; y < upload.y1; ++y) {
            DEFAULT_INT off = y * SCREEN_WIDTH + upload.x0;
            my_memcpy32(cfx_board_cache + off, bg_game + off, width);
        }
    }

    cfx_game_begin_offscreen_render();
    cfx_renderer3d_set_framebuffer(&renderer3d, cfx_board_cache);
    cfx_render_dirty_reset();
    face_count = 0;
    cfx_draw_grid_cached_ordered_fast();
    face_count = 0;
    cfx_render_dirty_reset();
    cfx_renderer3d_set_framebuffer(&renderer3d, GAME_FRAMEBUFFER);
    cfx_game_end_offscreen_render();

    cfx_board_cache_valid = 1;
    cfx_board_cache_rebuild_pending = 0;
    cfx_board_cache_visible_rect = new_visible;

    if (!upload.active) {
        return;
    }

    for (DEFAULT_INT y = upload.y0; y < upload.y1; ++y) {
        DEFAULT_INT off = y * SCREEN_WIDTH + upload.x0;
        my_memcpy32(GAME_FRAMEBUFFER + off, cfx_board_cache + off, upload.x1 - upload.x0);
    }

#if PLATFORM == NECPCFX
    cfx_game_pcfx_set_board_restore_source((const uint8_t *)cfx_board_cache);
    cfx_game_pcfx_board_cache_rect_changed((const uint8_t *)cfx_board_cache,
                                           upload.x0, upload.y0,
                                           upload.x1 - upload.x0,
                                           upload.y1 - upload.y0);
#endif
    cfx_render_dirty_add_rect(upload.x0, upload.y0, upload.x1, upload.y1);
}

static void cfx_board_cache_rebuild(void)
{
    my_memcpy32(cfx_board_cache, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);

    cfx_game_begin_offscreen_render();
    cfx_renderer3d_set_framebuffer(&renderer3d, cfx_board_cache);
    cfx_render_dirty_reset();
    face_count = 0;
    draw_grid();
    draw_sorted_faces();

    face_count = 0;
    cfx_render_dirty_reset();
    cfx_renderer3d_set_framebuffer(&renderer3d, GAME_FRAMEBUFFER);
    cfx_game_end_offscreen_render();
    cfx_board_cache_valid = 1;
    cfx_board_cache_rebuild_pending = 0;
    cfx_board_cache_visible_rect = cfx_grid_region_dirty_rect(0, 0, GRID_WIDTH, GRID_HEIGHT);
}

static void cfx_board_cache_rebuild_to_screen(void)
{
    cfx_board_cache_rebuild();
    my_memcpy32(GAME_FRAMEBUFFER, cfx_board_cache, SCREEN_WIDTH * SCREEN_HEIGHT);
#if PLATFORM == NECPCFX
    /* Push the rebuilt locked-field bitmap to the current KING page immediately.
       The PC-FX port will lazily seed the other page the first time gameplay
       renders to it, then uses the same 2D cache to restore stale page-flip
       rectangles before the live active 3D piece is drawn. */
    cfx_game_pcfx_set_board_restore_source((const uint8_t *)cfx_board_cache);
    cfx_game_pcfx_board_cache_changed();
    cfx_game_prepare_dirty_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    cfx_game_pcfx_restore_board_cache_rect((const uint8_t *)cfx_board_cache, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
#endif
    cfx_render_dirty_add_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

// Function to check collision
bool check_collision(DEFAULT_INT new_x, DEFAULT_INT new_y, DEFAULT_INT new_rotation) {
    DEFAULT_INT type = current_piece.type;
    DEFAULT_INT rotation = new_rotation;
    const int32_t *piece_shape = puzzle_pieces;

    for (DEFAULT_INT i = 0; i < 4; i++) {
        for (DEFAULT_INT j = 0; j < 4; j++) {
			DEFAULT_INT index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
            if (piece_shape[index]) {
                uint32_t x = new_x + j;
                DEFAULT_INT y = new_y + i;

                if (x >= GRID_WIDTH || y >= GRID_HEIGHT) return true;
                if (y >= 0 && grid[y * GRID_WIDTH + x]) return true;
            }
        }
    }
    return false;
}

// Function to clear full lines and set background flag
DEFAULT_INT clear_lines() {
    DEFAULT_INT lines_cleared = 0;

    for (DEFAULT_INT i = GRID_HEIGHT - 1; i >= 0; i--) {
        bool full = true;
        for (DEFAULT_INT j = 0; j < GRID_WIDTH; j++) {
            if (grid[i * GRID_WIDTH + j] == 0) {
                full = false;
                break;
            }
        }
        if (full) {
            lines_cleared++;
            PLAY_SFX(0, EXPLOSION_SFX);
            for (DEFAULT_INT k = i; k > 0; k--) {
                for (DEFAULT_INT j = 0; j < GRID_WIDTH; j++) {
                    grid[k * GRID_WIDTH + j] = grid[(k - 1) * GRID_WIDTH + j];
                }
            }
            for (DEFAULT_INT j = 0; j < GRID_WIDTH; j++) {
                grid[j] = 0;
            }
            i++;
        }
    }

    if (lines_cleared > 0) {
        DEFAULT_INT add = cfx_tetris_line_score(lines_cleared);
        if (score <= (DEFAULT_INT)(30000 - add)) {
            score += add;
        } else {
            score = 30000;
        }
    }

    return lines_cleared;
}



// Function to check almost losing condition
void check_almost_losing() {
    almost_losing = false;
    DEFAULT_INT *row = grid + 2 * GRID_WIDTH;
    for (DEFAULT_INT j = 0; j < GRID_WIDTH; j++) {
        if (row[j]) {
            almost_losing = true;
            return;
        }
    }
}

// Function to merge piece into grid
void merge_piece() {
    DEFAULT_INT type = current_piece.type;
    DEFAULT_INT rotation = current_piece.rotation;
    DEFAULT_INT grid_x = current_piece.x;
    DEFAULT_INT grid_y = current_piece.y;
    const int32_t *piece_shape = puzzle_pieces;

    for (DEFAULT_INT i = 0; i < 4; i++) {
        for (DEFAULT_INT j = 0; j < 4; j++) {
            DEFAULT_INT index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
            if (piece_shape[index]) {
                DEFAULT_INT x = grid_x + j;
                DEFAULT_INT y = grid_y + i;

                if (y >= 0 && y < GRID_HEIGHT && x >= 0 && x < GRID_WIDTH) {
                    grid[y * GRID_WIDTH + x] = type + 1;
                }
            }
        }
    }

    // Check for game over condition (if any block reaches the top row)
    DEFAULT_INT *top_row = grid;
    for (DEFAULT_INT j = 0; j < GRID_WIDTH; j++) {
        if (top_row[j]) {
            game_over = true;
            break;
        }
    }

    // Clear lines and set background if lines are cleared
    cfx_last_lines_cleared = clear_lines();
}

// Function to draw text
void PrintText(const char* str, DEFAULT_INT x, DEFAULT_INT y) 
{
    DEFAULT_INT maxw = 0;
    DEFAULT_INT linew = 0;
    DEFAULT_INT lines = 1;
    const char *scan = str;
    while (*scan) {
        if (*scan == '\n') {
            if (linew > maxw) maxw = linew;
            linew = 0;
            ++lines;
        } else {
            linew += 8;
        }
        ++scan;
    }
    if (linew > maxw) maxw = linew;
    cfx_game_prepare_dirty_rect(x - 1, y - 1, maxw + 2, (lines * 8) + 2);
    print_string(str, 255, CFX_TEXT_OUTLINE_INDEX, x, y, (uint8_t*)GAME_FRAMEBUFFER);
    cfx_game_upload_rect_pixels((const uint8_t *)GAME_FRAMEBUFFER, x - 1, y - 1, maxw + 2, (lines * 8) + 2);
    cfx_mark_text_dirty(str, x, y);
}

#if PLATFORM == NECPCFX
static void cfx_redraw_menu_overlay_pcfx(DEFAULT_INT menu_selection)
{
    cfx_game_pcfx_vdc_draw_menu(menu_selection);
    cfx_render_dirty_reset();
}

#endif

// Game States
typedef enum {
    GAME_STATE_TITLE,
    GAME_STATE_MENU,
    GAME_STATE_ARCADE,
    GAME_STATE_PUZZLE,
    GAME_STATE_GAME_OVER_ARCADE,
    GAME_STATE_GAME_OVER_PUZZLE,
    GAME_STATE_CREDITS
} GameState;

GameState game_state = GAME_STATE_TITLE;

// Uninitiliazed memory
static uint64_t mynext;

static inline int32_t myrand(void)
{
    mynext = mynext * 1103515245 + 12345;
    return (unsigned int)(mynext/65536) % 32768;
}

static inline void mysrand(int32_t seed)
{
    mynext = seed;
}

static int32_t cfx_arcade_drop_interval_for_level(DEFAULT_INT level)
{
    /* TGM-like gravity curve in millisecond steps.  The early curve has been
       tightened so the game stops feeling like it waits on input/rendering,
       while the late game still reaches 20G-like behavior. */
    if (level < 0) level = 0;
    if (level < 30)  return 620;
    if (level < 60)  return 450;
    if (level < 90)  return 320;
    if (level < 120) return 235;
    if (level < 160) return 170;
    if (level < 200) return 120;
    if (level < 250) return  85;
    if (level < 300) return  58;
    if (level < 350) return  40;
    if (level < 400) return  25;
    if (level < 450) return  18;
    if (level < 500) return  12;
    return 12;
}

static uint8_t cfx_arcade_is_20g(void)
{
    return (uint8_t)(arcade_level >= 500);
}

static int32_t cfx_arcade_das_delay_ms(void)
{
    /* Slower DAS start gives precise single-tap/edge placement on PC-FX pads. */
    if (arcade_level < 300) return 145;
    if (arcade_level < 500) return 120;
    return 96;
}

static int32_t cfx_arcade_arr_ms(void)
{
    /* Avoid 60 Hz sideways auto-slide; one repeated cell every few frames is
       controllable while still allowing held movement. */
    if (arcade_level < 300) return 48;
    if (arcade_level < 500) return 40;
    return 32;
}

static void cfx_arcade_reset_lock(void)
{
    cfx_arcade_lock_started_ms = -1;
    cfx_arcade_lock_resets = 0;
    cfx_arcade_was_grounded = 0;
}

static void cfx_arcade_reset_das(void)
{
    cfx_arcade_das_dir = 0;
    cfx_arcade_das_start_ms = 0;
    cfx_arcade_arr_last_ms = 0;
}

static void cfx_arcade_reset_difficulty(void)
{
    arcade_level = 0;
    arcade_lines_cleared = 0;
    arcade_piece_count = 0;
    cfx_arcade_hold_piece = -1;
    cfx_arcade_hold_used = 0;
    cfx_arcade_irs_buffer = 0;
    cfx_arcade_ihs_buffer = 0;
    cfx_arcade_phase = CFX_ARCADE_PHASE_ACTIVE;
    cfx_arcade_phase_until_ms = 0;
    cfx_arcade_reset_lock();
    cfx_arcade_reset_das();
    drop_interval = cfx_arcade_drop_interval_for_level(arcade_level);
}

static void cfx_arcade_set_level(DEFAULT_INT level)
{
    if (level < 0) level = 0;
    if (level > 999) level = 999;
    arcade_level = level;
    drop_interval = cfx_arcade_drop_interval_for_level(arcade_level);
}

static void cfx_arcade_advance_level(DEFAULT_INT amount)
{
    if (amount <= 0) return;
    cfx_arcade_set_level((DEFAULT_INT)(arcade_level + amount));
}

static void cfx_arcade_note_piece_lock(DEFAULT_INT lines)
{
    arcade_piece_count++;
    arcade_lines_cleared += lines;

    /* TGM-style level pressure: locking a piece advances level, and clearing
       lines adds extra level steps.  This is deliberately softened by the 12x12
       CascadeFX field and the existing 3D presentation. */
    cfx_arcade_advance_level((DEFAULT_INT)(1 + ((lines > 0) ? lines : 0)));
}

static void cfx_arcade_note_line_clear(DEFAULT_INT lines)
{
    (void)lines;
    /* Kept for older call sites; Arcade level is advanced at lock time so ARE
       and line-clear delay can use the new level consistently. */
}

static DEFAULT_INT cfx_tetris_line_score(DEFAULT_INT lines)
{
    static const DEFAULT_INT base_scores[5] = {0, 100, 300, 500, 800};
    if (lines < 0) {
        lines = 0;
    }
    if (lines > 4) {
        lines = 4;
    }
    return (DEFAULT_INT)(base_scores[lines] + (lines * (arcade_level + 1)));
}

static void cfx_arcade_buffer_initial(uint16_t held, uint16_t pressed)
{
    if (held & (CFX_INPUT_A | CFX_INPUT_UP)) {
        cfx_arcade_irs_buffer = 1;
    }

    /* IHS/hold should require an intentional fresh B press.  Treating a still-held
       B as a new hold during ARE made the button feel too sensitive and could
       consume the next piece before the player released it. */
    if ((pressed & CFX_INPUT_B) && (cfx_now_ms - cfx_arcade_last_hold_ms) >= 180) {
        cfx_arcade_ihs_buffer = 1;
        cfx_arcade_last_hold_ms = cfx_now_ms;
    }
}

static DEFAULT_INT cfx_arcade_next_piece_type(void)
{
    return (DEFAULT_INT)(myrand() % 7);
}

static uint8_t cfx_piece_try_apply_initial_rotation(void)
{
    if (!cfx_arcade_irs_buffer) {
        return 0;
    }
    DEFAULT_INT old_x = current_piece.x;
    DEFAULT_INT old_y = current_piece.y;
    DEFAULT_INT old_rotation = current_piece.rotation;
    DEFAULT_INT new_rotation = (DEFAULT_INT)((current_piece.rotation + 1) & 3);
    if (!check_collision(current_piece.x, current_piece.y, new_rotation)) {
        current_piece.rotation = new_rotation;
        cfx_rotation_anim_start(old_x, old_y, old_rotation, current_piece.x, current_piece.y, new_rotation);
        return 1;
    }
    return 0;
}

static void cfx_arcade_start_active_piece(DEFAULT_INT piece_type)
{
    cfx_rotation_anim_clear();
    cfx_move_anim_clear();
    current_piece.type = piece_type;
    current_piece.rotation = 0;
    current_piece.x = GRID_WIDTH / 2 - 2;
    current_piece.y = -4;
    cfx_arcade_hold_used = 0;
    cfx_arcade_reset_lock();
    cfx_physics_clock_reset(&drop_clock, cfx_now_ms);

    cfx_piece_try_apply_initial_rotation();

    cfx_arcade_irs_buffer = 0;
    cfx_arcade_ihs_buffer = 0;
    cfx_arcade_phase = CFX_ARCADE_PHASE_ACTIVE;

    if (check_collision(current_piece.x, current_piece.y, current_piece.rotation)) {
        game_state = GAME_STATE_GAME_OVER_ARCADE;
    }
}

static void cfx_arcade_spawn_with_ihs(DEFAULT_INT new_piece_type)
{
    DEFAULT_INT piece_type = new_piece_type;
    if (cfx_arcade_ihs_buffer) {
        if (cfx_arcade_hold_piece < 0) {
            cfx_arcade_hold_piece = new_piece_type;
            piece_type = cfx_arcade_next_piece_type();
        } else {
            DEFAULT_INT tmp = cfx_arcade_hold_piece;
            cfx_arcade_hold_piece = new_piece_type;
            piece_type = tmp;
        }
    }
    cfx_arcade_start_active_piece(piece_type);
}

static void cfx_arcade_schedule_are(DEFAULT_INT lines)
{
    (void)lines;

    /* No artificial ARE or line-clear delay.  The board/cache update has already
       completed before this function is called, so spawn the next piece in the
       same game update instead of waiting on a timing phase. */
    cfx_arcade_reset_lock();
    cfx_shadow_snapshot_clear();
    force_redraw = 1;
    force_redraw_puzzle = 1;
    cfx_arcade_spawn_with_ihs(cfx_arcade_next_piece_type());
}

static uint8_t cfx_piece_grounded(void)
{
    return (uint8_t)check_collision(current_piece.x, (DEFAULT_INT)(current_piece.y + 1), current_piece.rotation);
}

static void cfx_arcade_reset_lock_on_action(void)
{
    if (cfx_piece_grounded() && cfx_arcade_lock_started_ms >= 0 && cfx_arcade_lock_resets < 8) {
        cfx_arcade_lock_started_ms = cfx_now_ms;
        cfx_arcade_lock_resets++;
    }
}

static uint8_t cfx_arcade_try_shift(DEFAULT_INT dx)
{
    if (!check_collision((DEFAULT_INT)(current_piece.x + dx), current_piece.y, current_piece.rotation)) {
        int32_t from_world_x256;
        int32_t from_world_y256;
        DEFAULT_INT old_x = current_piece.x;
        DEFAULT_INT old_y = current_piece.y;
        cfx_move_anim_current_origin256(&from_world_x256, &from_world_y256);
        current_piece.x = (DEFAULT_INT)(current_piece.x + dx);
        cfx_move_anim_start_from_origin(from_world_x256, from_world_y256,
                                        old_x, old_y, current_piece.x, current_piece.y,
                                        current_piece.rotation);
        cfx_arcade_reset_lock_on_action();
        force_redraw = 1;
        return 1;
    }
    return 0;
}

static uint8_t cfx_arcade_try_rotate(DEFAULT_INT delta)
{
    DEFAULT_INT old_x = current_piece.x;
    DEFAULT_INT old_y = current_piece.y;
    DEFAULT_INT old_rotation = current_piece.rotation;
    DEFAULT_INT new_rotation = (DEFAULT_INT)((current_piece.rotation + delta) & 3);
    static const DEFAULT_INT kicks[] = {0, -1, 1, -2, 2};

    for (DEFAULT_INT i = 0; i < (DEFAULT_INT)(sizeof(kicks) / sizeof(kicks[0])); ++i) {
        DEFAULT_INT test_x = (DEFAULT_INT)(current_piece.x + kicks[i]);
        if (!check_collision(test_x, current_piece.y, new_rotation)) {
            current_piece.x = test_x;
            current_piece.rotation = new_rotation;
            cfx_rotation_anim_start(old_x, old_y, old_rotation, current_piece.x, current_piece.y, new_rotation);
            cfx_arcade_reset_lock_on_action();
            force_redraw = 1;
            return 1;
        }
    }
    return 0;
}

static uint8_t cfx_arcade_try_hold(void)
{
    if (cfx_arcade_hold_used || (cfx_now_ms - cfx_arcade_last_hold_ms) < 180) {
        return 0;
    }
    cfx_arcade_last_hold_ms = cfx_now_ms;

    DEFAULT_INT incoming;
    if (cfx_arcade_hold_piece < 0) {
        cfx_arcade_hold_piece = current_piece.type;
        incoming = cfx_arcade_next_piece_type();
    } else {
        incoming = cfx_arcade_hold_piece;
        cfx_arcade_hold_piece = current_piece.type;
    }

    cfx_arcade_irs_buffer = 0;
    cfx_arcade_start_active_piece(incoming);
    cfx_arcade_hold_used = 1;
    force_redraw = 1;
    return 1;
}

static void cfx_arcade_charge_das(uint16_t held)
{
    DEFAULT_INT dir = 0;
    if (held & CFX_INPUT_LEFT) {
        dir = 1; /* Game's logical left increases x. */
    } else if (held & CFX_INPUT_RIGHT) {
        dir = -1;
    }

    if (dir == 0) {
        cfx_arcade_reset_das();
        return;
    }

    if (dir != cfx_arcade_das_dir) {
        cfx_arcade_das_dir = dir;
        cfx_arcade_das_start_ms = cfx_now_ms;
        cfx_arcade_arr_last_ms = cfx_now_ms;
    }
}

static void cfx_arcade_apply_das(uint16_t held, uint16_t pressed)
{
    DEFAULT_INT dir = 0;
    if (pressed & CFX_INPUT_LEFT) {
        dir = 1;
    } else if (pressed & CFX_INPUT_RIGHT) {
        dir = -1;
    }

    cfx_arcade_charge_das(held);

    if (dir != 0) {
        cfx_arcade_try_shift(dir);
        return;
    }

    if (cfx_arcade_das_dir != 0 && (held & (CFX_INPUT_LEFT | CFX_INPUT_RIGHT))) {
        int32_t das_elapsed = cfx_now_ms - cfx_arcade_das_start_ms;
        int32_t arr = cfx_arcade_arr_ms();
        DEFAULT_INT repeats = 0;
        while (das_elapsed >= cfx_arcade_das_delay_ms() &&
               cfx_now_ms - cfx_arcade_arr_last_ms >= arr && repeats < 1) {
            cfx_arcade_try_shift(cfx_arcade_das_dir);
            cfx_arcade_arr_last_ms += arr;
            repeats++;
        }
        if (repeats >= 1) {
            cfx_arcade_arr_last_ms = cfx_now_ms;
        }
    }
}

static DEFAULT_INT cfx_puzzle_das_dir = 0;
static int32_t cfx_puzzle_das_start_ms = 0;
static int32_t cfx_puzzle_arr_last_ms = 0;

static void cfx_puzzle_reset_das(void)
{
    cfx_puzzle_das_dir = 0;
    cfx_puzzle_das_start_ms = 0;
    cfx_puzzle_arr_last_ms = 0;
}

static uint8_t cfx_puzzle_try_shift(DEFAULT_INT dir)
{
    DEFAULT_INT next_x = (DEFAULT_INT)(current_piece.x + dir);
    if (!check_collision(next_x, current_piece.y, current_piece.rotation)) {
        int32_t from_world_x256;
        int32_t from_world_y256;
        DEFAULT_INT old_x = current_piece.x;
        DEFAULT_INT old_y = current_piece.y;
        cfx_move_anim_current_origin256(&from_world_x256, &from_world_y256);
        current_piece.x = next_x;
        cfx_move_anim_start_from_origin(from_world_x256, from_world_y256,
                                        old_x, old_y, current_piece.x, current_piece.y,
                                        current_piece.rotation);
        force_redraw = 1;
        return 1;
    }
    return 0;
}

static void cfx_puzzle_apply_das(uint16_t held, uint16_t pressed)
{
    DEFAULT_INT pressed_dir = 0;
    if (pressed & CFX_INPUT_LEFT) {
        pressed_dir = 1;
    } else if (pressed & CFX_INPUT_RIGHT) {
        pressed_dir = -1;
    }

    DEFAULT_INT held_dir = 0;
    if (held & CFX_INPUT_LEFT) {
        held_dir = 1;
    } else if (held & CFX_INPUT_RIGHT) {
        held_dir = -1;
    }

    if (!held_dir) {
        cfx_puzzle_reset_das();
        return;
    }

    if (held_dir != cfx_puzzle_das_dir) {
        cfx_puzzle_das_dir = held_dir;
        cfx_puzzle_das_start_ms = cfx_now_ms;
        cfx_puzzle_arr_last_ms = cfx_now_ms;
    }

    if (pressed_dir) {
        cfx_puzzle_try_shift(pressed_dir);
        return;
    }

    DEFAULT_INT repeats = 0;
    while (cfx_now_ms - cfx_puzzle_das_start_ms >= 145 &&
           cfx_now_ms - cfx_puzzle_arr_last_ms >= 48 && repeats < 1) {
        cfx_puzzle_try_shift(cfx_puzzle_das_dir);
        cfx_puzzle_arr_last_ms += 48;
        repeats++;
    }
    if (repeats >= 1) {
        cfx_puzzle_arr_last_ms = cfx_now_ms;
    }
}

static void cfx_arcade_apply_gravity(DEFAULT_INT steps)
{
    if (cfx_arcade_is_20g()) {
        while (!check_collision(current_piece.x, (DEFAULT_INT)(current_piece.y + 1), current_piece.rotation)) {
            current_piece.y++;
            force_redraw = 1;
        }
        return;
    }

    while (steps > 0) {
        steps--;
        if (!check_collision(current_piece.x, (DEFAULT_INT)(current_piece.y + 1), current_piece.rotation)) {
            current_piece.y++;
            cfx_arcade_lock_started_ms = -1;
            cfx_arcade_was_grounded = 0;
            force_redraw = 1;
        } else {
            break;
        }
    }
}

static uint8_t cfx_arcade_should_lock(uint16_t held)
{
    (void)held;

    /* No artificial lock delay.  Once the active piece is grounded, merge it
       immediately in the same gameplay update.  This removes the old 500..180 ms
       grounded wait and the separate 80 ms soft-drop wait that made placed pieces
       sit visibly on the stack even while the frame counter still read 60 FPS. */
    if (!cfx_piece_grounded()) {
        cfx_arcade_lock_started_ms = -1;
        cfx_arcade_was_grounded = 0;
        return 0;
    }

    cfx_arcade_lock_started_ms = cfx_now_ms;
    cfx_arcade_was_grounded = 1;
    return 1;
}

static void cfx_arcade_lock_current_piece(void)
{
    PLAY_SFX(0, DROP_SFX);
    Piece locked = current_piece;
    merge_piece();
    if (game_over) {
        Game_Switch(GAME_STATE_GAME_OVER_ARCADE);
        return;
    }

    cfx_arcade_note_piece_lock(cfx_last_lines_cleared);
    if (cfx_last_lines_cleared == 0) {
        cfx_board_cache_update_locked_piece_region(&locked);
    } else {
        cfx_board_cache_rebuild_fast_ordered_to_screen();
    }
    cfx_arcade_schedule_are(cfx_last_lines_cleared);
}

static void cfx_arcade_hard_drop_and_lock(void)
{
    cfx_rotation_anim_clear();
    cfx_move_anim_clear();
    while (!check_collision(current_piece.x, (DEFAULT_INT)(current_piece.y + 1), current_piece.rotation)) {
        current_piece.y++;
    }
    force_redraw = 1;
    cfx_arcade_lock_current_piece();
}

static void cfx_arcade_update_are(uint16_t held, uint16_t pressed)
{
    cfx_arcade_buffer_initial(held, pressed);
    cfx_arcade_charge_das(held);

    if (cfx_now_ms >= cfx_arcade_phase_until_ms) {
        cfx_arcade_spawn_with_ihs(cfx_arcade_next_piece_type());
        force_redraw = 1;
    }
}

static void cfx_arcade_update_active(uint16_t held, uint16_t pressed, DEFAULT_INT gravity_steps)
{
    if (pressed & CFX_INPUT_B) {
        cfx_arcade_try_hold();
    }

    cfx_arcade_apply_das(held, pressed);

    if (pressed & (CFX_INPUT_A | CFX_INPUT_UP)) {
        cfx_arcade_try_rotate(1);
    }

    if (pressed & CFX_INPUT_HARD_DROP) {
        cfx_arcade_hard_drop_and_lock();
        return;
    }

    if (held & CFX_INPUT_DOWN) {
        if (!check_collision(current_piece.x, (DEFAULT_INT)(current_piece.y + 1), current_piece.rotation)) {
            current_piece.y++;
            cfx_arcade_lock_started_ms = -1;
            cfx_arcade_was_grounded = 0;
            force_redraw = 1;
        }
    }

    if (gravity_steps > 0 || cfx_arcade_is_20g()) {
        cfx_arcade_apply_gravity(gravity_steps);
    }

    if (cfx_arcade_should_lock(held)) {
        cfx_arcade_lock_current_piece();
    }
}

// Function to spawn a new piece in Arcade Mode
void spawn_piece() {
    cfx_arcade_spawn_with_ihs(cfx_arcade_next_piece_type());
}

Puzzle puzzles[MAX_PUZZLES];
DEFAULT_INT current_puzzle_index = 0;
DEFAULT_INT puzzle_piece_index = 0;

// Function to initialize puzzles
void initialize_puzzles() {
    DEFAULT_INT puzzle_index;
    for (puzzle_index = 0; puzzle_index < MAX_PUZZLES; puzzle_index++) {
        // Clear the current puzzle structure
        memset(&puzzles[puzzle_index], 0, sizeof(Puzzle));
    }
	puzzles[0].num_pieces = 1;
	puzzles[0].piece_sequence[0] = 1; // 'O' piece

	puzzles[1].num_pieces = 2;
	puzzles[1].piece_sequence[0] = 2; // 'T' piece
	puzzles[1].piece_sequence[1] = 0; // 'I' piece

	puzzles[2].num_pieces = 4;
	puzzles[2].piece_sequence[0] = 0; // 'I' piece
	puzzles[2].piece_sequence[1] = 0; // 'I' piece
	puzzles[2].piece_sequence[2] = 0; // 'I' piece
	puzzles[2].piece_sequence[3] = 1; // 'O' piece

	puzzles[3].num_pieces = 5;
	puzzles[3].piece_sequence[0] = 2; // 'T' piece
	puzzles[3].piece_sequence[1] = 0; // 'I' piece
	puzzles[3].piece_sequence[2] = 0; // 'I' piece
	puzzles[3].piece_sequence[3] = 0; // 'I' piece
	puzzles[3].piece_sequence[4] = 0; // 'I' piece
}




// Function to spawn a new piece in Training
void spawn_piece_puzzle() {
    cfx_rotation_anim_clear();
    cfx_move_anim_clear();
    Puzzle *puzzle = &puzzles[current_puzzle_index];
    if (puzzles[current_puzzle_index].num_pieces - puzzle_piece_index == 0) {
        // No more pieces; check if puzzle is solved
        bool puzzle_solved = true;
        for (DEFAULT_INT i = 0; i < GRID_HEIGHT * GRID_WIDTH; i++) {
            if (grid[i]) {
                puzzle_solved = false;
                break;
            }
        }

        if (puzzle_solved) {
            // Load next puzzle
            current_puzzle_index++;
            load_puzzle(current_puzzle_index);
            drop_interval = DEFAULT_INTERVAL;
        } else {
            // Puzzle failed
            Game_Switch(GAME_STATE_GAME_OVER_PUZZLE); 
        }
        return;
    }

    current_piece.type = puzzle->piece_sequence[puzzle_piece_index++];
    current_piece.rotation = 0;
    current_piece.x = GRID_WIDTH / 2 - 2;
    current_piece.y = -4;

    clear_background_frames = 0;

    if (check_collision(current_piece.x, current_piece.y, current_piece.rotation)) {
        Game_Switch(GAME_STATE_GAME_OVER_PUZZLE); 
    }
}


// Function to load a puzzle
void load_puzzle(DEFAULT_INT index) {
	backtitle = false;
    if (index >= MAX_PUZZLES) {
        // No more puzzles
        Game_Switch(GAME_STATE_TITLE);
        backtitle = true;
        return;
    }

	memcpy(
		grid,
		&initial_grids[index * GRID_HEIGHT * GRID_WIDTH],
		GRID_HEIGHT * GRID_WIDTH * sizeof(DEFAULT_INT)
	);
   // memcpy(grid, puzzle->initial_grid, sizeof(grid));
   
    puzzle_piece_index = 0;
    score = 0;
    cfx_last_lines_cleared = 0;
    game_over = false;
    clear_background_frames = 0;
    almost_losing = false;
    spawn_piece_puzzle();
    cfx_board_cache_invalidate();
    return;
}


#define CFX_TITLE_CUBE_MARGIN_X 0
#define CFX_TITLE_CUBE_MARGIN_Y 0

typedef struct {
    Point3D transformed_vertices[8];
    Point2D projected_points[8];
    DEFAULT_INT face_depths[6];
    DEFAULT_INT face_order[6];
    CfxRenderDirty face_dirty[6];
    uint8_t backface_mask;
    CfxRenderDirty dirty;
} CfxTitleCubeFrame;


static void cfx_title_cube_project_vertices(Point3D *transformed_vertices, Point2D *projected_points,
                                            DEFAULT_INT angle_x, DEFAULT_INT angle_y, DEFAULT_INT angle_z,
                                            DEFAULT_INT cube_position_x, DEFAULT_INT cube_position_y,
                                            DEFAULT_INT distance_cube_titlescreen)
{
    (void)angle_z;

    for (DEFAULT_INT i = 0; i < 8; i++) {
        Point3D v = cube_vertices_template[i];

        // Adjust the size of the cube
        v.x = (cfx_mul_i32(v.x , distance_cube_titlescreen)) / DISTANCE_CUBE;
        v.y = (cfx_mul_i32(v.y , distance_cube_titlescreen)) / DISTANCE_CUBE;
        v.z = (cfx_mul_i32(v.z , distance_cube_titlescreen)) / DISTANCE_CUBE;

        // Rotate cube
        v = rotateX(v, angle_x);
        v = rotateY(v, angle_y);
        //v = rotateZ(v, angle_z);

        // Translate cube
        v.x += cube_position_x;
        v.y += cube_position_y;
        v.z += STARTING_Z_OFFSET;

        transformed_vertices[i] = v;

        // Project vertices and store the projected points
#ifdef FAST_DRAWING
        projected_points[i].x = -v.x;
        projected_points[i].y = -v.y;
        projected_points[i].u = 0;
        projected_points[i].v = 0;
#else
        {
            const DEFAULT_INT distance = PROJECTION_DISTANCE;
            DEFAULT_INT factor = Division(INT_TO_FIXED(distance), (distance - v.z));
            projected_points[i].x = FIXED_TO_INT(cfx_mul_i32(v.x, factor));
            projected_points[i].y = FIXED_TO_INT(cfx_mul_i32(v.y, factor));
            projected_points[i].u = 0;
            projected_points[i].v = 0;
        }
#endif

        projected_points[i].x += SCREEN_WIDTH_HALF;
        projected_points[i].y += SCREEN_HEIGHT_HALF;
    }
}

static void cfx_dirty_from_points(CfxRenderDirty *r, const Point2D *p, DEFAULT_INT count, DEFAULT_INT margin_x, DEFAULT_INT margin_y)
{
    if (count <= 0) {
        return;
    }

    DEFAULT_INT x0 = p[0].x;
    DEFAULT_INT y0 = p[0].y;
    DEFAULT_INT x1 = p[0].x;
    DEFAULT_INT y1 = p[0].y;

    for (DEFAULT_INT i = 1; i < count; i++) {
        if (p[i].x < x0) x0 = p[i].x;
        if (p[i].y < y0) y0 = p[i].y;
        if (p[i].x > x1) x1 = p[i].x;
        if (p[i].y > y1) y1 = p[i].y;
    }

    CfxRenderDirty q;
    q.x0 = x0 - margin_x;
    q.y0 = y0 - margin_y;
    q.x1 = x1 + margin_x + 1;
    q.y1 = y1 + margin_y + 1;
    q.active = 1;

    if (q.x0 < 0) q.x0 = 0;
    if (q.y0 < 0) q.y0 = 0;
    if (q.x1 > SCREEN_WIDTH) q.x1 = SCREEN_WIDTH;
    if (q.y1 > SCREEN_HEIGHT) q.y1 = SCREEN_HEIGHT;
    if (q.x0 >= q.x1 || q.y0 >= q.y1) {
        q.active = 0;
    }

    cfx_render_dirty_union(r, &q);
}

static void cfx_title_cube_prepare(CfxTitleCubeFrame *cube,
                                   DEFAULT_INT angle_x, DEFAULT_INT angle_y, DEFAULT_INT angle_z,
                                   DEFAULT_INT cube_position_x, DEFAULT_INT cube_position_y,
                                   DEFAULT_INT distance_cube_titlescreen)
{
    cube->dirty.x0 = SCREEN_WIDTH;
    cube->dirty.y0 = SCREEN_HEIGHT;
    cube->dirty.x1 = 0;
    cube->dirty.y1 = 0;
    cube->dirty.active = 0;
    cube->backface_mask = 0;
    for (DEFAULT_INT i = 0; i < 6; i++) {
        cube->face_dirty[i].x0 = SCREEN_WIDTH;
        cube->face_dirty[i].y0 = SCREEN_HEIGHT;
        cube->face_dirty[i].x1 = 0;
        cube->face_dirty[i].y1 = 0;
        cube->face_dirty[i].active = 0;
    }

    cfx_title_cube_project_vertices(cube->transformed_vertices, cube->projected_points,
                                    angle_x, angle_y, angle_z,
                                    cube_position_x, cube_position_y,
                                    distance_cube_titlescreen);

    for (DEFAULT_INT i = 0; i < 6; i++) {
        cube->face_order[i] = i;

        DEFAULT_INT idx0 = cube_faces_template[i * 4 + 0];
        DEFAULT_INT idx1 = cube_faces_template[i * 4 + 1];
        DEFAULT_INT idx2 = cube_faces_template[i * 4 + 2];
        DEFAULT_INT idx3 = cube_faces_template[i * 4 + 3];

        Point3D *v0 = &cube->transformed_vertices[idx0];
        Point3D *v1 = &cube->transformed_vertices[idx1];
        Point3D *v2 = &cube->transformed_vertices[idx2];
        Point3D *v3 = &cube->transformed_vertices[idx3];

        int32_t ax = v1->x - v0->x;
        int32_t ay = v1->y - v0->y;
        int32_t bx = v2->x - v0->x;
        int32_t by = v2->y - v0->y;
        int32_t nz = cfx_mul_i32(ax , by) - cfx_mul_i32(ay , bx);

        if (nz > 0) {
            cube->backface_mask = (uint8_t)(cube->backface_mask | (uint8_t)(1u << i));
        } else {
            Point2D face_points[4] = {
                cube->projected_points[idx0], cube->projected_points[idx1],
                cube->projected_points[idx2], cube->projected_points[idx3]
            };
            cfx_dirty_from_points(&cube->face_dirty[i], face_points, 4, CFX_TITLE_CUBE_MARGIN_X, CFX_TITLE_CUBE_MARGIN_Y);
            cfx_render_dirty_align_x16(&cube->face_dirty[i]);
            cfx_render_dirty_union(&cube->dirty, &cube->face_dirty[i]);
        }

        cube->face_depths[i] = v0->z + v1->z + v2->z + v3->z;
    }

    /* Sort faces from farthest to nearest.  The list is only six entries, but
       doing it in the prepare pass avoids a second transform/cull/depth pass on
       every title frame. */
    for (DEFAULT_INT i = 0; i < 5; i++) {
        for (DEFAULT_INT j = i + 1; j < 6; j++) {
            if (cube->face_depths[cube->face_order[i]] > cube->face_depths[cube->face_order[j]]) {
                DEFAULT_INT temp = cube->face_order[i];
                cube->face_order[i] = cube->face_order[j];
                cube->face_order[j] = temp;
            }
        }
    }

    if (!cube->dirty.active) {
        cfx_dirty_from_points(&cube->dirty, cube->projected_points, 8, CFX_TITLE_CUBE_MARGIN_X, CFX_TITLE_CUBE_MARGIN_Y);
    }

    cfx_render_dirty_align_x16(&cube->dirty);
}

#if PLATFORM == NECPCFX
static void cfx_title_cube_mark_pcfx_dirty(const CfxTitleCubeFrame *cube, const CfxRenderDirty *prompt_dirty)
{
    DEFAULT_INT emitted = 0;
    for (DEFAULT_INT k = 0; k < 6; k++) {
        DEFAULT_INT i = cube->face_order[k];
        if ((cube->backface_mask & (uint8_t)(1u << i)) || !cube->face_dirty[i].active) {
            continue;
        }
        REFRESH_SCREEN_RECT(cube->face_dirty[i].x0, cube->face_dirty[i].y0,
                            cube->face_dirty[i].x1 - cube->face_dirty[i].x0,
                            cube->face_dirty[i].y1 - cube->face_dirty[i].y0);
        emitted = 1;
    }

#ifndef FAST_DRAWING
    if (prompt_dirty && prompt_dirty->active) {
        REFRESH_SCREEN_RECT(prompt_dirty->x0, prompt_dirty->y0,
                            prompt_dirty->x1 - prompt_dirty->x0,
                            prompt_dirty->y1 - prompt_dirty->y0);
        emitted = 1;
    }
#else
    (void)prompt_dirty;
#endif

    if (!emitted && cube->dirty.active) {
        REFRESH_SCREEN_RECT(cube->dirty.x0, cube->dirty.y0,
                            cube->dirty.x1 - cube->dirty.x0,
                            cube->dirty.y1 - cube->dirty.y0);
    }
}
#endif

static void cfx_title_cube_draw_prepared(const CfxTitleCubeFrame *cube)
{
    for (DEFAULT_INT k = 0; k < 6; k++) {
        DEFAULT_INT i = cube->face_order[k];

        if (cube->backface_mask & (uint8_t)(1u << i)) {
            continue;
        }

        DEFAULT_INT idx0 = cube_faces_template[i * 4 + 0];
        DEFAULT_INT idx1 = cube_faces_template[i * 4 + 1];
        DEFAULT_INT idx2 = cube_faces_template[i * 4 + 2];
        DEFAULT_INT idx3 = cube_faces_template[i * 4 + 3];

        DEFAULT_INT tex_coord_offset = i * 4 * 2;
        Point2D p0 = cube->projected_points[idx0];
        Point2D p1 = cube->projected_points[idx1];
        Point2D p2 = cube->projected_points[idx2];
        Point2D p3 = cube->projected_points[idx3];

        p0.u = cube_texcoords_template[tex_coord_offset + 0];
        p0.v = cube_texcoords_template[tex_coord_offset + 1];
        p1.u = cube_texcoords_template[tex_coord_offset + 2];
        p1.v = cube_texcoords_template[tex_coord_offset + 3];
        p2.u = cube_texcoords_template[tex_coord_offset + 4];
        p2.v = cube_texcoords_template[tex_coord_offset + 5];
        p3.u = cube_texcoords_template[tex_coord_offset + 6];
        p3.v = cube_texcoords_template[tex_coord_offset + 7];

        cfx_renderer3d_draw_quad(&renderer3d, &p0, &p1, &p2, &p3, 1);
    }
}

void draw_title_cube(DEFAULT_INT angle_x, DEFAULT_INT angle_y, DEFAULT_INT angle_z, DEFAULT_INT cube_position_x, DEFAULT_INT cube_position_y, DEFAULT_INT distance_cube_titlescreen){
    CfxTitleCubeFrame cube;
    cfx_title_cube_prepare(&cube, angle_x, angle_y, angle_z,
                           cube_position_x, cube_position_y,
                           distance_cube_titlescreen);
    cfx_title_cube_draw_prepared(&cube);
}


// Add these variables at the beginning of main, with the other variable declarations
DEFAULT_INT a_button = 0;
DEFAULT_INT b_button = 0;
DEFAULT_INT up_button = 0;
DEFAULT_INT down_button = 0;
DEFAULT_INT left_button = 0;
DEFAULT_INT right_button = 0;
DEFAULT_INT start_button = 0;
DEFAULT_INT select_button = 0;
DEFAULT_INT hold_down_button = 0;

/* Game buttons are edge-triggered by the selected input backend.
   main/game logic consumes only these fields through input_port.h. */

DEFAULT_INT title_cube_rotation_x = 0;
DEFAULT_INT title_cube_rotation_y = 0;
DEFAULT_INT title_cube_rotation_z = 0;
// Variables to control cube movement
DEFAULT_INT title_cube_position_x = 0;
DEFAULT_INT title_cube_position_z = 0;
DEFAULT_INT z_counter = 0;
DEFAULT_INT title_cube_move_direction = 1;
const DEFAULT_INT title_cube_move_speed = 1; // Adjust the speed as needed
DEFAULT_INT title_cube_max_position = 64; // Maximum movement to the right
DEFAULT_INT title_cube_min_position = -64; // Maximum movement to the left;
static CfxRenderDirty cfx_title_prev_cube_dirty = {0, 0, 0, 0, 0};
#if PLATFORM == NECPCFX
#define CFX_TITLE_PROMPT_TEXT "PUSH RUN TO START"
#define CFX_TITLE_PROMPT_X (SCREEN_WIDTH_HALF - 68)
#define CFX_TITLE_PROMPT_Y 192
#define CFX_TITLE_PROMPT_W (17 * 8)
#else
#define CFX_TITLE_PROMPT_TEXT "Press Start to Play"
#define CFX_TITLE_PROMPT_X (SCREEN_WIDTH_HALF - 80)
#define CFX_TITLE_PROMPT_Y 170
#define CFX_TITLE_PROMPT_W (19 * 8)
#endif
#define CFX_TITLE_PROMPT_H 10
#define CFX_MENU_DIRTY_X (SCREEN_WIDTH_HALF - 64)
#define CFX_MENU_DIRTY_Y 156
#define CFX_MENU_DIRTY_W 128
#define CFX_MENU_DIRTY_H 80
DEFAULT_INT angle_x = 0; // Slight angle to see the top
DEFAULT_INT angle_y = 0;
static DEFAULT_INT cfx_menu_last_selection = -1;
static uint8_t cfx_menu_force_redraw = 1;
#if PLATFORM == NECPCFX
static uint8_t cfx_pcfx_first_state_switch = 1;
#endif

DEFAULT_INT mus = 0;

DEFAULT_INT alt_state = GAME_STATE_TITLE;
void Game_Switch(DEFAULT_INT state)
{
	alt_state = state;
#if PLATFORM != NECPCFX && PLATFORM != CASLOOPY
	cfx_game_platform_state_enter(state);
#endif

	mus++;

	switch(state)
	{
		case GAME_STATE_TITLE:
#if PLATFORM == NECPCFX
			if (!cfx_pcfx_first_state_switch) {
				cfx_game_fade_out();
			}
			cfx_game_platform_state_enter(state);
			cfx_pcfx_first_state_switch = 0;
#endif
		
			cfx_title_prev_cube_dirty.active = 0;

			// Set active texture to index 1 texture
			texture_actual = texture + (1 * (TEXTURE_SIZE_EACH));
		
			cfx_game_palette_empty();
		
			z_counter = 0;
#if PLATFORM == 3
			/* Loopy screenshots/regression runs should show the cube immediately.
			   Keep the PC-FX/headless title intro timing unchanged. */
			title_cube_position_z = 24;
#else
			title_cube_position_z = 0;
#endif
			cfx_puzzle_reset_das();
			

			my_memcpy32(GAME_FRAMEBUFFER, bg_title, (SCREEN_WIDTH * SCREEN_HEIGHT));
			
			// Text routines do have in fact a significant performance penalty on Amiga CD32 since they are being drawn every frame due to overlapping with 3D cube
#ifdef FAST_DRAWING
			PrintText("Press Start to Play", SCREEN_WIDTH_HALF - 80, 190);
#endif
			REFRESH_SCREEN(0, SCREEN_WIDTH * SCREEN_HEIGHT);
			PRESENT_SCREEN();
#if PLATFORM == NECPCFX
				cfx_game_pcfx_draw_static_title_prompt(CFX_TITLE_PROMPT_TEXT, CFX_TITLE_PROMPT_X, CFX_TITLE_PROMPT_Y);
#endif

			cfx_game_fade_in();
		break;
		case GAME_STATE_MENU:
#if PLATFORM == NECPCFX
			if (game_state == GAME_STATE_TITLE) {
				cfx_game_pcfx_fast_title_to_menu(CFX_TITLE_PROMPT_X, CFX_TITLE_PROMPT_Y,
										 CFX_TITLE_PROMPT_W, CFX_TITLE_PROMPT_H);
			} else {
				cfx_game_platform_state_enter(state);
			}
			cfx_pcfx_first_state_switch = 0;
#endif
			cfx_menu_force_redraw = 1;
			cfx_menu_last_selection = -1;
#if PLATFORM != NECPCFX
			my_memcpy32(GAME_FRAMEBUFFER, bg_title, SCREEN_WIDTH * SCREEN_HEIGHT);
			REFRESH_SCREEN(0, SCREEN_WIDTH * SCREEN_HEIGHT);
			PRESENT_SCREEN();
#endif
		break;
		case GAME_STATE_ARCADE:
#if PLATFORM == NECPCFX
			/* Start the voice cue before the visual/music transition.  Previously it
			   fired after fade-out and CDDA retargeting, so it felt late when entering
			   Arcade from the menu. */
			PLAY_SFX(0, READY_SFX);
			if (!cfx_pcfx_first_state_switch) {
				cfx_game_fade_out();
			}
			cfx_game_platform_state_enter(state);
			cfx_pcfx_first_state_switch = 0;
#else
			cfx_game_fade_out();
			PLAY_SFX(0, READY_SFX);
#endif
			
			
			my_memcpy32(GAME_FRAMEBUFFER, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
			REFRESH_SCREEN(0, SCREEN_WIDTH*SCREEN_HEIGHT);
			PRESENT_SCREEN();
			cfx_game_fade_in();


			cfx_arcade_reset_difficulty();
			cfx_puzzle_reset_das();
			cfx_physics_clock_reset(&drop_clock, cfx_now_ms);
		
			// Initialize Arcade Mode
			memset(grid, 0, sizeof(grid));
			cfx_last_lines_cleared = 0;
			score = 0;
			game_over = false;
			clear_background_frames = 0;
			almost_losing = false;
			spawn_piece();
			cfx_board_cache_invalidate();
			force_redraw = 1;
			backtitle = 0;

			
		break;
		case GAME_STATE_PUZZLE:

#if PLATFORM == NECPCFX
			/* Training uses the same ready voice cue as Arcade; fire it before the
			   screen transition so the cue leads the scene change. */
			PLAY_SFX(0, READY_SFX);
			if (!cfx_pcfx_first_state_switch) {
				cfx_game_fade_out();
			}
			cfx_game_platform_state_enter(state);
			cfx_pcfx_first_state_switch = 0;
#else
			cfx_game_fade_out();
			PLAY_SFX(0, READY_SFX);
#endif

			my_memcpy32(GAME_FRAMEBUFFER, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
			REFRESH_SCREEN(0, SCREEN_WIDTH * SCREEN_HEIGHT);
			PRESENT_SCREEN();
			cfx_game_fade_in();

			drop_interval = DEFAULT_INTERVAL;
			cfx_puzzle_reset_das();
				cfx_physics_clock_reset(&drop_clock, cfx_now_ms);
			
			// Initialize Training
			current_puzzle_index = 0;
			load_puzzle(current_puzzle_index);
			clear_background_frames = 0;
			force_redraw = 1;
			force_redraw_puzzle = 1;
			backtitle = 0;

			cfx_board_cache_invalidate();

		break;
		case GAME_STATE_GAME_OVER_ARCADE:
#if PLATFORM == NECPCFX
			cfx_game_platform_state_enter(state);
			cfx_pcfx_first_state_switch = 0;
#endif
			PLAY_SFX(0, GO_SFX);

			memset(GAME_FRAMEBUFFER, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
#if PLATFORM == CASLOOPY
			cfx_game_platform_state_enter(state);
#endif
				cfx_rotation_anim_clear();
				cfx_move_anim_clear();
				cfx_shadow_snapshot_clear();
				cfx_prev_active_draw_rect.active = 0;
				force_redraw = 0;
				force_redraw_puzzle = 0;
#if PLATFORM == NECPCFX
			cfx_game_pcfx_show_game_over_screen();
#endif
		break;
		case GAME_STATE_GAME_OVER_PUZZLE:
#if PLATFORM == NECPCFX
			cfx_game_platform_state_enter(state);
			cfx_pcfx_first_state_switch = 0;
#endif
			PLAY_SFX(0, GO_SFX);

			memset(GAME_FRAMEBUFFER, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
#if PLATFORM == CASLOOPY
			cfx_game_platform_state_enter(state);
#endif
				cfx_rotation_anim_clear();
				cfx_move_anim_clear();
				cfx_shadow_snapshot_clear();
				cfx_prev_active_draw_rect.active = 0;
				force_redraw = 0;
				force_redraw_puzzle = 0;
#if PLATFORM == NECPCFX
			cfx_game_pcfx_show_game_over_screen();
#endif
		break;
		case GAME_STATE_CREDITS:
#if PLATFORM == NECPCFX
			cfx_game_platform_state_enter(state);
			cfx_pcfx_first_state_switch = 0;
#endif
			memset(GAME_FRAMEBUFFER, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
		break;
	}
}

DEFAULT_INT cubes_to_process = 0;

void draw_grid_partial() {
    // Define the area around the current piece to redraw
    const DEFAULT_INT REDRAW_MARGIN = 4;  // Adjust this value based on your needs

    DEFAULT_INT start_y = max(0, current_piece.y - REDRAW_MARGIN);
    DEFAULT_INT end_y = min(GRID_HEIGHT - 1, current_piece.y + REDRAW_MARGIN);
    DEFAULT_INT start_x = max(0, current_piece.x - REDRAW_MARGIN);
    DEFAULT_INT end_x = min(GRID_WIDTH - 1, current_piece.x + REDRAW_MARGIN);

	//cubes_to_process = 0;

    // Only draw grid cells within the defined area
    for (DEFAULT_INT i = start_y; i <= end_y; i++) {
        for (DEFAULT_INT j = start_x; j <= end_x; j++) {
            DEFAULT_INT cell_value = grid[i * GRID_WIDTH + j];
            if (cell_value) {
                DEFAULT_INT tetromino_type = cell_value - 1;
                cfx_emit_cached_cell_faces(j, i, tetromino_type, cfx_grid_cell_occupied);
                //cubes_to_process++;
            }
        }
    }
}




static void cfx_puzzle_lock_current_piece_now(void)
{
    PLAY_SFX(0, DROP_SFX);
    Piece locked = current_piece;
    cfx_rotation_anim_clear();
    cfx_move_anim_clear();
    merge_piece();
    if (game_over) {
        Game_Switch(GAME_STATE_GAME_OVER_PUZZLE);
    } else {
        if (cfx_last_lines_cleared == 0) {
            cfx_board_cache_update_locked_piece_region(&locked);
        } else {
            cfx_board_cache_rebuild_fast_ordered_to_screen();
        }
        spawn_piece_puzzle();
        force_redraw_puzzle = 1;
    }
    if (backtitle == 0) {
        force_redraw = 1;
    }
}

static void cfx_puzzle_hard_drop_and_lock(void)
{
    cfx_rotation_anim_clear();
    cfx_move_anim_clear();
    while (!check_collision(current_piece.x, (DEFAULT_INT)(current_piece.y + 1), current_piece.rotation)) {
        current_piece.y++;
    }
    cfx_puzzle_lock_current_piece_now();
}

DEFAULT_INT Init_video_game()
{
    return cfx_game_platform_init();
}

int cfx_game_run(void)
{
	if (Init_video_game() == 1)
	{
		return 0;
	}
	
#ifdef FAST_DIV
	init_div_lutr_fast();
#endif
	initDivs();

	{
		CfxRenderer3DConfig render_config;
		render_config.framebuffer = GAME_FRAMEBUFFER;
		render_config.width = SCREEN_WIDTH;
		render_config.height = SCREEN_HEIGHT;
		cfx_renderer3d_init(&renderer3d, &render_config);
		cfx_renderer3d_set_texture_atlas(&renderer3d, texture, CFX_TEXTURE_TILE_PITCH_BYTES, CFX_TEXTURE_TILE_STRIDE_BYTES);
		cfx_precompute_cell_faces();
	}

    // Initialize game state
    memset(grid, 0, sizeof(grid));
    cfx_board_cache_invalidate();
    score = 0;
    game_over = false;
    clear_background_frames = 0;
    almost_losing = false;

    // Initialize puzzles
    initialize_puzzles();
	DEFAULT_INT running = 1;

    int32_t current_tick = 0;
    int32_t drop_due = 0;
    CfxInputFrame input_frame;
    memset(&input_frame, 0, sizeof(input_frame));
    cfx_physics_clock_reset(&drop_clock, 0);

    DEFAULT_INT blink_counter = 0;
    bool blink_on = true;

    // Menu variables
    DEFAULT_INT menu_selection = 0; // 0: Arcade, 1: Training
    
    Game_Switch(GAME_STATE_TITLE);
    
	cfx_input_clear_game_buttons();

    while (running) 
    {
		cfx_renderer3d_set_framebuffer(&renderer3d, GAME_FRAMEBUFFER);
		cfx_render_dirty_reset();
		{
			input_frame = cfx_input_poll_game_frame(&drop_clock, drop_interval);
			current_tick = input_frame.now_ms;
			cfx_now_ms = current_tick;
			drop_due = input_frame.drop_due;
			if (!input_frame.running) {
				running = 0;
			}
			mysrand(current_tick);
		}
		
		// Handle game states
        switch (game_state) 
        {
			case GAME_STATE_TITLE:
#if PLATFORM == NECPCFX
				if ((input_frame.held & CFX_INPUT_START) || BUTTON_PRESSED(start_button)) {
					Game_Switch(GAME_STATE_MENU);
					cfx_redraw_menu_overlay_pcfx(menu_selection);
					/* Draw immediately for the frame that accepted RUN, but keep the
					   menu marked dirty so the first real menu-state frame redraws
					   after the deferred state commit.  Some PC-FX command/bios timing
					   paths present the post-transition frame only after that commit. */
					cfx_menu_last_selection = -1;
					cfx_menu_force_redraw = 1;
					cfx_title_prev_cube_dirty.active = 0;
					break;
				}
#endif
				// Update rotation angles
				title_cube_rotation_x = (title_cube_rotation_x + 1) & ANGLE_MASK;
				title_cube_rotation_y = (title_cube_rotation_y + 2) & ANGLE_MASK;
				title_cube_rotation_z = (title_cube_rotation_z + 3) & ANGLE_MASK;
				
                // Blinking text
                blink_counter++;

                if (title_cube_position_z < 24)
                {
					z_counter++;
					if (z_counter > 2)
					{
						title_cube_position_z++;
						z_counter = 0;
					}
				}
				else
				{
					// Update cube position
					title_cube_position_x += title_cube_move_direction * title_cube_move_speed;
					if (title_cube_position_x > title_cube_max_position) {
						title_cube_position_x = title_cube_max_position;
						title_cube_move_direction = -1; // Change direction to left
					} else if (title_cube_position_x < title_cube_min_position) {
						title_cube_position_x = title_cube_min_position;
						title_cube_move_direction = 1; // Change direction to right
					}	
				}

				CfxTitleCubeFrame title_cube_frame;
				cfx_title_cube_prepare(&title_cube_frame, title_cube_rotation_x, title_cube_rotation_y, title_cube_rotation_z,
									 title_cube_position_x, -20, title_cube_position_z);
#ifdef FORCE_FULLSCREEN_DRAWS
				my_memcpy32(GAME_FRAMEBUFFER, bg_title, SCREEN_WIDTH * SCREEN_HEIGHT);
#else
				CfxRenderDirty title_cube_dirty = title_cube_frame.dirty;
				CfxRenderDirty title_prompt_dirty = {CFX_TITLE_PROMPT_X - 2, CFX_TITLE_PROMPT_Y - 2,
											CFX_TITLE_PROMPT_X + CFX_TITLE_PROMPT_W + 2,
											CFX_TITLE_PROMPT_Y + CFX_TITLE_PROMPT_H + 2, 1};
#if PLATFORM == NECPCFX
				/* PC-FX tracks stale foreground on each KING page.  Do not merge the
				   previous cube into the current dirty list: the back page's own stale
				   face rectangles are cleared by cfx_game_prepare_dirty_rect(), and
				   recording the old union here needlessly turns three face spans into a
				   much larger rectangle on the next visit to this page. */
				cfx_restore_background_rect(bg_title, &title_cube_dirty);
#else
				CfxRenderDirty title_update_dirty = title_cube_dirty;
				cfx_render_dirty_union(&title_update_dirty, &cfx_title_prev_cube_dirty);
#ifndef FAST_DRAWING
				cfx_render_dirty_union(&title_update_dirty, &title_prompt_dirty);
#endif
				cfx_render_dirty_align_x16(&title_update_dirty);
				cfx_restore_background_rect(bg_title, &title_update_dirty);
#endif
#endif
				// Draw rotating cube with updated position
				cfx_title_cube_draw_prepared(&title_cube_frame);
#ifndef FORCE_FULLSCREEN_DRAWS
				/* On PC-FX the 3D renderer writes foreground spans directly to KING KRAM.
				   Other targets use no-op upload hooks here, so avoid the old post-render
				   rectangle copy from the CPU staging framebuffer. */
				cfx_title_prev_cube_dirty = title_cube_dirty;
#endif

#ifndef FAST_DRAWING
#if PLATFORM != NECPCFX
					if (blink_counter < 60)
					{
						PrintText(CFX_TITLE_PROMPT_TEXT, CFX_TITLE_PROMPT_X, CFX_TITLE_PROMPT_Y);
					}
					if (blink_counter >= 120)
					{
						blink_counter = 0;
					}
#endif
#endif

#if PLATFORM != NECPCFX
					if (BUTTON_PRESSED(start_button)) {
						Game_Switch(GAME_STATE_MENU);
						break;
				}
#endif
				
#ifdef FORCE_FULLSCREEN_DRAWS
				REFRESH_SCREEN(0, SCREEN_WIDTH * SCREEN_HEIGHT);
#else
#if PLATFORM == NECPCFX
				cfx_title_cube_mark_pcfx_dirty(&title_cube_frame, NULL);
#else
				REFRESH_SCREEN_RECT(title_update_dirty.x0, title_update_dirty.y0,
									title_update_dirty.x1 - title_update_dirty.x0,
									title_update_dirty.y1 - title_update_dirty.y0);
#endif
#endif

				
                break;
            case GAME_STATE_MENU:
				if (BUTTON_PRESSED(up_button)) {
					PLAY_SFX(0, EXPLOSION_SFX);
					menu_selection--;
				}
				if (BUTTON_PRESSED(down_button)) {
					PLAY_SFX(0, EXPLOSION_SFX);
					menu_selection++;
				}
				
				if (menu_selection < 0) menu_selection = 0;
				if (menu_selection > 1) menu_selection = 2;
				

				if (BUTTON_PRESSED(a_button)) 
				{
					if (menu_selection == 0) 
					{
						Game_Switch(GAME_STATE_ARCADE);
					} else if (menu_selection == 1) {
						Game_Switch(GAME_STATE_PUZZLE);
					} else if (menu_selection == 2) {
						Game_Switch(GAME_STATE_CREDITS);
					}
					break;
				}

				if (cfx_menu_force_redraw || cfx_menu_last_selection != menu_selection) {
#if PLATFORM == NECPCFX
					cfx_redraw_menu_overlay_pcfx(menu_selection);
#else
#ifdef FORCE_FULLSCREEN_DRAWS
					my_memcpy32(GAME_FRAMEBUFFER, bg_title, SCREEN_WIDTH * SCREEN_HEIGHT);
#else
					CfxRenderDirty menu_dirty = {CFX_MENU_DIRTY_X, CFX_MENU_DIRTY_Y,
											CFX_MENU_DIRTY_X + CFX_MENU_DIRTY_W,
											CFX_MENU_DIRTY_Y + CFX_MENU_DIRTY_H, 1};
					cfx_render_dirty_align_x16(&menu_dirty);
					cfx_restore_background_rect(bg_title, &menu_dirty);
#endif

					PrintText("Select Mode", SCREEN_WIDTH_HALF - 50, 160);

					switch(menu_selection)
					{
						case 0:
							PrintText("> Arcade Mode", SCREEN_WIDTH_HALF - 50, 180);
							PrintText("  Training", SCREEN_WIDTH_HALF - 50, 200);
							PrintText("  Credits", SCREEN_WIDTH_HALF - 50, 220);
						break;
						case 1:
							PrintText("  Arcade Mode", SCREEN_WIDTH_HALF - 50, 180);
							PrintText("> Training", SCREEN_WIDTH_HALF - 50, 200);
							PrintText("  Credits", SCREEN_WIDTH_HALF - 50, 220);
						break;
						case 2:
							PrintText("  Arcade Mode", SCREEN_WIDTH_HALF - 50, 180);
							PrintText("  Training", SCREEN_WIDTH_HALF - 50, 200);
							PrintText("> Credits", SCREEN_WIDTH_HALF - 50, 220);
						break;
					}

#ifdef FORCE_FULLSCREEN_DRAWS
					REFRESH_SCREEN(0, SCREEN_WIDTH * SCREEN_HEIGHT);
#else
					cfx_game_mark_menu_dirty(menu_dirty.x0, menu_dirty.y0, menu_dirty.x1 - menu_dirty.x0, menu_dirty.y1 - menu_dirty.y0);
#endif
#endif
					cfx_menu_last_selection = menu_selection;
					cfx_menu_force_redraw = 0;
				}
                
                break;
            case GAME_STATE_ARCADE:
            case GAME_STATE_PUZZLE:
                // Reset face count before collecting faces
                face_count = 0;
				
				if (!game_over) 
				{
					if (game_state == GAME_STATE_ARCADE)
					{
						if (cfx_arcade_phase == CFX_ARCADE_PHASE_ARE)
						{
							cfx_arcade_update_are(input_frame.held, input_frame.pressed);
						}
						else
						{
							cfx_arcade_update_active(input_frame.held, input_frame.pressed, (DEFAULT_INT)drop_due);
						}
						drop_due = 0;
					}
					else
					{
						cfx_puzzle_apply_das(input_frame.held, input_frame.pressed);
						if (DPAD_PRESSED(down_button)) {
							if (!check_collision(current_piece.x, current_piece.y + 1, current_piece.rotation))
							{
								current_piece.y++;
								force_redraw = 1;
							}
							else
							{
								cfx_physics_drop_force(&drop_clock, drop_interval);
								drop_due = 1;
							}
						}
						if (BUTTON_PRESSED(a_button) || BUTTON_PRESSED(up_button)) {
							DEFAULT_INT old_x = current_piece.x;
							DEFAULT_INT old_y = current_piece.y;
							DEFAULT_INT old_rotation = current_piece.rotation;
							DEFAULT_INT new_rotation = (current_piece.rotation + 1) & 3;
							if (!check_collision(current_piece.x, current_piece.y, new_rotation))
							{
								current_piece.rotation = new_rotation;
								cfx_rotation_anim_start(old_x, old_y, old_rotation, current_piece.x, current_piece.y, new_rotation);
								force_redraw = 1;
							}
						}

						if (input_frame.pressed & CFX_INPUT_HARD_DROP) {
							cfx_puzzle_hard_drop_and_lock();
							drop_due = 0;
						}

						// Automatic drop
						while (drop_due > 0 && !game_over) 
						{
							drop_due--;
							if (!check_collision(current_piece.x, current_piece.y + 1, current_piece.rotation)) {
								current_piece.y++;
								force_redraw = 1;
							} 
							else 
							{
									cfx_puzzle_lock_current_piece_now();
							}
						}
					}
				}

				if (cfx_rotation_anim.active || cfx_move_anim.active)
				{
					force_redraw = 1;
				}

				// Before calling the functions
				if (force_redraw) 
				{
#if PLATFORM == NECPCFX
                    if (cfx_board_cache_valid) {
                        cfx_game_pcfx_set_board_restore_source((const uint8_t *)cfx_board_cache);
                    }
#endif
#ifdef FORCE_FULLSCREEN_DRAWS // For platforms that are fast enough, this can avoid some visual glitches
					if (cfx_board_cache_rebuild_pending || !cfx_board_cache_valid) {
						cfx_board_cache_rebuild_to_screen();
					} else {
						my_memcpy32(GAME_FRAMEBUFFER, cfx_board_cache, SCREEN_WIDTH * SCREEN_HEIGHT);
						cfx_render_dirty_add_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
					}
#else
					if (cfx_board_cache_rebuild_pending || !cfx_board_cache_valid)
					{
						cfx_board_cache_rebuild_to_screen();
					}
					else
					{
						undraw_previous_piece();
					}
#endif
					if (!(game_state == GAME_STATE_ARCADE && cfx_arcade_phase == CFX_ARCADE_PHASE_ARE))
					{
						CfxRenderDirty active_dirty = cfx_active_render_dirty_rect(current_piece.type,
												 current_piece.rotation,
												 current_piece.x,
												 current_piece.y);
						cfx_restore_board_rect(&active_dirty);
						cfx_draw_landing_ghost_piece();
						cfx_draw_active_piece_field();
						draw_current_piece();
						draw_sorted_faces();
						cfx_draw_active_piece_field_cache_repair();
					}
#if PLATFORM == NECPCFX
						/* PC-FX gameplay HUD text lives on the dual VDCs, like the title
						   prompt. Keep KING for blocks/ghost/active 3D only. */
						if (game_state == GAME_STATE_PUZZLE) {
							DEFAULT_INT remaining_pieces = puzzles[current_puzzle_index].num_pieces - puzzle_piece_index;
							cfx_game_pcfx_vdc_game_hud_update(1, remaining_pieces);
						} else {
							cfx_game_pcfx_vdc_game_hud_update(0, score);
						}
#else
						cfx_draw_gameplay_hud_panel((uint8_t)(game_state == GAME_STATE_PUZZLE));
						if (game_state == GAME_STATE_PUZZLE) {
							// Display remaining pieces
							DEFAULT_INT remaining_pieces = puzzles[current_puzzle_index].num_pieces - puzzle_piece_index;
							PrintText("Left", 10, 10);
							PrintText(myitoa(remaining_pieces), 10, 30);
						}
						else
						{
							// Display score
							PrintText(myitoa(score), 10, 10);
						}
#endif

					cfx_render_dirty_commit();

					force_redraw = 0;
					force_redraw_puzzle = 0;
				}


                break;
            case GAME_STATE_GAME_OVER_ARCADE:
            case GAME_STATE_GAME_OVER_PUZZLE:
				if (BUTTON_PRESSED(start_button) || BUTTON_PRESSED(a_button)) {
					Game_Switch(GAME_STATE_TITLE);
				}
#if PLATFORM == CASLOOPY
                /* The Loopy game-over bitmap is built once during the state
                   transition by the platform layer, then maintained by the
                   normal double-buffer dirty-page path. */
#elif PLATFORM != NECPCFX
                // Draw game over texts
                PrintText("Game Over!", SCREEN_WIDTH_HALF - 40, SCREEN_HEIGHT_HALF - 10);
                PrintText("Press Start to return", SCREEN_WIDTH_HALF - 80, SCREEN_HEIGHT_HALF + 10);

				REFRESH_SCREEN(0, SCREEN_WIDTH*SCREEN_HEIGHT);
#endif

                break;
            case GAME_STATE_CREDITS:
                // Clear screen
                memset(GAME_FRAMEBUFFER, 0, SCREEN_WIDTH * SCREEN_HEIGHT);

                // Draw game over texts
                PrintText("CREDITS", 16, 16);
                PrintText("Main Programmer : Gameblabla", 16, 32);
                PrintText("Graphics : SDXL", 16, 48);
                PrintText("Snds: sfxr.me", 16, 64);
                
				if (BUTTON_PRESSED(start_button) || BUTTON_PRESSED(a_button)) {
					Game_Switch(GAME_STATE_TITLE);
				}
				
				REFRESH_SCREEN(0, SCREEN_WIDTH*SCREEN_HEIGHT);
            break;
            default:
                break;
        }
        


        game_state = alt_state;
        cfx_runtime_present_frame(current_tick);

    }

    return 0;
}
