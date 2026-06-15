#include "cfx_game_platform.h"
#include "defines.h"
#include "cd32.h"
#include <string.h>

int __nocommandline = 1;
static const uint8_t gamepal[768] = {0};
static uint8_t texture[CFX_TEXTURE_ATLAS_WIDTH * CFX_TEXTURE_ATLAS_HEIGHT];
static uint8_t bg_game[SCREEN_WIDTH * SCREEN_HEIGHT];
static uint8_t bg_title[SCREEN_WIDTH * SCREEN_HEIGHT];

uint8_t *cfx_game_framebuffer(void) { return gfxbuf; }
const uint8_t *cfx_game_bg_game(void) { return bg_game; }
const uint8_t *cfx_game_bg_title(void) { return bg_title; }
const uint8_t *cfx_game_texture_atlas(void) { return texture; }

void cfx_game_memcpy(void *dst, const void *src, size_t bytes) { memcpy(dst, src, bytes); }
void cfx_game_mark_dirty(int index, int length) { (void)index; (void)length; Draw_Video_Akiko(); }
void cfx_game_mark_dirty_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; Draw_Video_Akiko(); }
void cfx_game_present_immediate(void) { Draw_Video_Akiko(); }
void cfx_game_mark_menu_dirty(int x, int y, int w, int h) { Draw_Video_Akiko_partial(x, y, x + w, y + h); }
void cfx_game_prepare_dirty_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void cfx_game_upload_rect_pixels(const uint8_t *src, int x, int y, int w, int h) { (void)src; (void)x; (void)y; (void)w; (void)h; }
void cfx_game_begin_offscreen_render(void) {}
void cfx_game_end_offscreen_render(void) {}
void cfx_game_palette_empty(void) {}
void cfx_game_fade_in(void) {}
void cfx_game_fade_out(void) {}
void cfx_game_play_sfx(int effect_id) { (void)effect_id; }
void cfx_game_adjust_undraw_min_x(DEFAULT_INT *min_x) { (void)min_x; }
void cfx_game_platform_state_enter(int state_id) { (void)state_id; }

int cfx_game_platform_init(void)
{
    Init_Video(320, 240, 320, 240, 1);
    SetPalette_Video(gamepal, 256);
    LoadFile_tobuffer("title320.raw", bg_title);
    LoadFile_tobuffer("bg320.raw", bg_game);
    LoadFile_tobuffer("textures.raw", texture);
    return 0;
}
