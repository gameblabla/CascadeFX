/* Host-side dense-board non-clear placement profiler. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "defines.h"
#include "common.h"
#include "physics.h"
#include "renderer3d.h"
#include "input_port.h"

static uint8_t bench_fb[SCREEN_WIDTH * SCREEN_HEIGHT];
static uint8_t bench_bg[SCREEN_WIDTH * SCREEN_HEIGHT];
static uint8_t bench_tex[CFX_TEXTURE_ATLAS_WIDTH * CFX_TEXTURE_ATLAS_HEIGHT];

uint8_t *cfx_game_framebuffer(void) { return bench_fb; }
const uint8_t *cfx_game_bg_game(void) { return bench_bg; }
const uint8_t *cfx_game_bg_title(void) { return bench_bg; }
const uint8_t *cfx_game_texture_atlas(void) { return bench_tex; }
int cfx_game_platform_init(void) { return 0; }
void cfx_game_platform_state_enter(int state_id) { (void)state_id; }
void cfx_game_memcpy(void *dst, const void *src, size_t bytes) { memcpy(dst, src, bytes); }
void cfx_game_mark_dirty(int index, int length) { (void)index; (void)length; }
void cfx_game_mark_dirty_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void cfx_game_present_immediate(void) {}
void cfx_game_mark_menu_dirty(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void cfx_game_prepare_dirty_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void cfx_game_upload_rect_pixels(const uint8_t *src, int x, int y, int w, int h) { (void)src; (void)x; (void)y; (void)w; (void)h; }
void cfx_game_begin_offscreen_render(void) {}
void cfx_game_end_offscreen_render(void) {}
void cfx_game_palette_empty(void) {}
void cfx_game_fade_in(void) {}
void cfx_game_fade_out(void) {}
void cfx_game_play_sfx(int effect_id) { (void)effect_id; }
void cfx_game_adjust_undraw_min_x(DEFAULT_INT *min_x) { *min_x -= 1; }
void cfx_game_pcfx_draw_static_title_prompt(const char *str, int x, int y) { (void)str; (void)x; (void)y; }
void cfx_game_pcfx_set_board_restore_source(const uint8_t *src) { (void)src; }
void cfx_game_pcfx_board_cache_changed(void) {}
void cfx_game_pcfx_board_cache_rect_changed(const uint8_t *src, int x, int y, int w, int h) { (void)src; (void)x; (void)y; (void)w; (void)h; }
void cfx_game_pcfx_board_cache_rect_changed_deferred_page(const uint8_t *src, int x, int y, int w, int h) { (void)src; (void)x; (void)y; (void)w; (void)h; }
void cfx_game_pcfx_restore_board_cache_rect(const uint8_t *src, int x, int y, int w, int h) { (void)src; (void)x; (void)y; (void)w; (void)h; }
void cfx_game_pcfx_upload_rect_all_pages(const uint8_t *src, int x, int y, int w, int h) { (void)src; (void)x; (void)y; (void)w; (void)h; }
void cfx_game_pcfx_upload_static_overlay_all_pages(const uint8_t *src) { (void)src; }
void cfx_game_pcfx_vdc_game_hud_reset(void) {}
void cfx_game_pcfx_vdc_game_hud_update(uint8_t puzzle_mode, int value) { (void)puzzle_mode; (void)value; }
void print_string(const char *s, const uint16_t fg_color, const uint16_t bg_color, int32_t x, int32_t y, uint8_t *buffer) { (void)s; (void)fg_color; (void)bg_color; (void)x; (void)y; (void)buffer; }
void cfx_input_clear_game_buttons(void) {}
CfxInputFrame cfx_input_poll_game_frame(CfxPhysicsClock *drop_clock, int32_t drop_interval) { (void)drop_clock; (void)drop_interval; CfxInputFrame f = {0,0,0,0,0}; return f; }
void cfx_runtime_present_frame(int32_t now_ms) { (void)now_ms; }

#include "src/game/cfx_game_runtime.c"

static void init_bench(void)
{
    initDivs();
    for (int i = 0; i < (int)sizeof(bench_tex); ++i) bench_tex[i] = (uint8_t)(1 + (i % 239));
    memset(bench_bg, 0, sizeof(bench_bg));
    CfxRenderer3DConfig cfg = {bench_fb, SCREEN_WIDTH, SCREEN_HEIGHT};
    cfx_renderer3d_init(&renderer3d, &cfg);
    cfx_renderer3d_set_texture_atlas(&renderer3d, bench_tex, CFX_TEXTURE_TILE_PITCH_BYTES, CFX_TEXTURE_TILE_STRIDE_BYTES);
    cfx_precompute_cell_faces();
    cfx_board_cache_rebuild();
}

static Piece setup_dense_nonclear_placement(int variant)
{
    memset(grid, 0, sizeof(grid));
    for (int y = 3; y < GRID_HEIGHT; ++y) {
        int hole = (y * 5 + 1) % GRID_WIDTH;
        for (int x = 0; x < GRID_WIDTH; ++x) {
            if (x == hole || ((x + y + variant) % 13) == 0) grid[y * GRID_WIDTH + x] = 0;
            else grid[y * GRID_WIDTH + x] = (uint8_t)((x + y + variant) % 7 + 1);
        }
    }

    Piece locked;
    locked.type = 2;      /* T */
    locked.rotation = (variant & 3);
    locked.x = 0 + (variant % 3);
    locked.y = GRID_HEIGHT - 5;

    /* Merge a T-like piece into a holey area, but keep every row non-full. */
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            int idx = GET_PUZZLE_PIECE_INDEX(locked.type, locked.rotation, i, j);
            if (puzzle_pieces[idx]) {
                int x = locked.x + j;
                int y = locked.y + i;
                if (x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) grid[y * GRID_WIDTH + x] = locked.type + 1;
            }
        }
    }

    for (int y = 0; y < GRID_HEIGHT; ++y) {
        grid[y * GRID_WIDTH + ((y * 5 + 1) % GRID_WIDTH)] = 0;
    }
    cfx_board_cache_valid = 1;
    cfx_board_cache_rebuild_pending = 0;
    return locked;
}

int main(int argc, char **argv)
{
    int iters = (argc > 1) ? atoi(argv[1]) : 5000;
    init_bench();
    clock_t t0 = clock();
    int accum = 0;
    for (int i = 0; i < iters; ++i) {
        Piece locked = setup_dense_nonclear_placement(i);
        if (i == 0) {
            CfxRenderDirty r = cfx_grid_region_dirty_rect(locked.x - 1, locked.y - 1, locked.x + 5, locked.y + 5);
            printf("place_rect %d %d %d %d active=%d\n", r.x0, r.y0, r.x1, r.y1, r.active);
        }
        cfx_board_cache_update_locked_piece_region(&locked);
        accum += bench_fb[(i * 37) % (SCREEN_WIDTH * SCREEN_HEIGHT)];
    }
    clock_t t1 = clock();
    printf("iters=%d seconds=%.6f accum=%d faces=%d\n", iters, (double)(t1 - t0) / CLOCKS_PER_SEC, accum, (int)face_count);
    return 0;
}
