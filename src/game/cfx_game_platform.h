#ifndef CFX_GAME_PLATFORM_H
#define CFX_GAME_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include "common.h"

/* Game-facing platform service layer.  The game/runtime code owns gameplay
   state only; ports own hardware headers, generated assets, framebuffers,
   palette fades, sound, and presentation. */

enum {
    CFX_SFX_DROP = 0,
    CFX_SFX_EXPLOSION = 1,
    CFX_SFX_SELECT = 2,
    CFX_SFX_READY = 3,
    CFX_SFX_GO = 4
};

uint8_t *cfx_game_framebuffer(void);
const uint8_t *cfx_game_bg_game(void);
const uint8_t *cfx_game_bg_title(void);
const uint8_t *cfx_game_texture_atlas(void);

int cfx_game_platform_init(void);
void cfx_game_platform_state_enter(int state_id);

void cfx_game_memcpy(void *dst, const void *src, size_t bytes);
void cfx_game_mark_dirty(int index, int length);
void cfx_game_mark_dirty_rect(int x, int y, int w, int h);
void cfx_game_present_immediate(void);
void cfx_game_mark_menu_dirty(int x, int y, int w, int h);

/* PC-FX uses KING as a transparent foreground over VDC backgrounds.
   Generic ports implement these as no-ops; the PC-FX port uses them to clear
   the target KRAM page before direct span writes and to suppress hardware
   writes while building off-screen board caches. */
void cfx_game_prepare_dirty_rect(int x, int y, int w, int h);
void cfx_game_upload_rect_pixels(const uint8_t *src, int x, int y, int w, int h);
void cfx_game_begin_offscreen_render(void);
void cfx_game_end_offscreen_render(void);

void cfx_game_palette_empty(void);
void cfx_game_fade_in(void);
void cfx_game_fade_out(void);
void cfx_game_play_sfx(int effect_id);
void cfx_game_adjust_undraw_min_x(DEFAULT_INT *min_x);

#if PLATFORM == NECPCFX
void cfx_game_pcfx_draw_static_title_prompt(const char *str, int x, int y);
void cfx_game_pcfx_set_board_restore_source(const uint8_t *src);
void cfx_game_pcfx_board_cache_changed(void);
void cfx_game_pcfx_board_cache_rect_changed(const uint8_t *src, int x, int y, int w, int h);
void cfx_game_pcfx_board_cache_rect_changed_deferred_page(const uint8_t *src, int x, int y, int w, int h);
void cfx_game_pcfx_restore_board_cache_rect(const uint8_t *src, int x, int y, int w, int h);
void cfx_game_pcfx_upload_rect_all_pages(const uint8_t *src, int x, int y, int w, int h);
void cfx_game_pcfx_upload_static_overlay_all_pages(const uint8_t *src);
void cfx_game_pcfx_fast_title_to_menu(int prompt_x, int prompt_y, int prompt_w, int prompt_h);
void cfx_game_pcfx_vdc_draw_menu(int menu_selection);
void cfx_game_pcfx_vdc_game_hud_reset(void);
void cfx_game_pcfx_vdc_game_hud_update(uint8_t puzzle_mode, int value);
void cfx_game_pcfx_show_game_over_screen(void);
#endif

#endif
