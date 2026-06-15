#include "cfx_game_platform.h"
#include "defines.h"
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <stdio.h>
#include <string.h>

uint8_t gamepal[768];
uint8_t texture[CFX_TEXTURE_ATLAS_WIDTH * CFX_TEXTURE_ATLAS_HEIGHT];
uint8_t bg_game[SCREEN_WIDTH * SCREEN_HEIGHT];
uint8_t bg_title[SCREEN_WIDTH * SCREEN_HEIGHT];
SDL_Surface *screen;
static SDL_Surface *texture_surface;

#define FPS_VIDEO 60
const float real_FPS = 1000 / FPS_VIDEO;

uint8_t *cfx_game_framebuffer(void) { return (uint8_t *)screen->pixels; }
const uint8_t *cfx_game_bg_game(void) { return bg_game; }
const uint8_t *cfx_game_bg_title(void) { return bg_title; }
const uint8_t *cfx_game_texture_atlas(void) { return texture; }

void cfx_game_memcpy(void *dst, const void *src, size_t bytes) { memcpy(dst, src, bytes); }
void cfx_game_mark_dirty(int index, int length) { (void)index; (void)length; }
void cfx_game_mark_dirty_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void cfx_game_present_immediate(void) { SDL_Flip(screen); }
void cfx_game_mark_menu_dirty(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
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

static int load_background(const char *filename, uint8_t *bg_array)
{
    SDL_Surface *bg_surface = IMG_Load(filename);
    if (!bg_surface) {
        fprintf(stderr, "Unable to load %s: %s\n", filename, IMG_GetError());
        return 0;
    }
    if (bg_surface->w != SCREEN_WIDTH || bg_surface->h != SCREEN_HEIGHT) {
        fprintf(stderr, "%s must be %d x %d pixels.\n", filename, SCREEN_WIDTH, SCREEN_HEIGHT);
        SDL_FreeSurface(bg_surface);
        return 0;
    }
    if (bg_surface->format->BitsPerPixel != 8) {
        fprintf(stderr, "%s must be an 8-bit image.\n", filename);
        SDL_FreeSurface(bg_surface);
        return 0;
    }
    if (!bg_surface->format->palette) {
        fprintf(stderr, "%s does not have a palette.\n", filename);
        SDL_FreeSurface(bg_surface);
        return 0;
    }
    if (memcmp(bg_surface->format->palette->colors, screen->format->palette->colors,
               sizeof(SDL_Color) * screen->format->palette->ncolors) != 0) {
        fprintf(stderr, "%s palette does not match screen palette.\n", filename);
        SDL_FreeSurface(bg_surface);
        return 0;
    }
    memcpy(bg_array, bg_surface->pixels, SCREEN_WIDTH * SCREEN_HEIGHT);
    SDL_FreeSurface(bg_surface);
    return 1;
}

int cfx_game_platform_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
    texture_surface = IMG_Load("textures.png");
    if (!texture_surface) {
        fprintf(stderr, "Unable to load texture: %s\n", IMG_GetError());
        SDL_Quit();
        return 1;
    }
    if (texture_surface->w != CFX_TEXTURE_ATLAS_WIDTH || texture_surface->h != CFX_TEXTURE_ATLAS_HEIGHT) {
        fprintf(stderr, "Texture must be 16x112 pixels.\n");
        SDL_FreeSurface(texture_surface);
        SDL_Quit();
        return 1;
    }
    screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 8, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_FreeSurface(texture_surface);
        SDL_Quit();
        return 1;
    }
    if (texture_surface->format->palette) {
        SDL_SetPalette(screen, SDL_LOGPAL | SDL_PHYSPAL,
                       texture_surface->format->palette->colors, 0,
                       texture_surface->format->palette->ncolors);
    } else {
        fprintf(stderr, "Texture does not have a palette.\n");
        SDL_FreeSurface(texture_surface);
        SDL_Quit();
        return 1;
    }
    memcpy(texture, texture_surface->pixels, CFX_TEXTURE_ATLAS_WIDTH * CFX_TEXTURE_ATLAS_HEIGHT);
    SDL_FreeSurface(texture_surface);
    if (!load_background("background320.png", bg_game)) {
        SDL_Quit();
        return 1;
    }
    if (!load_background("title320.png", bg_title)) {
        SDL_Quit();
        return 1;
    }
    return 0;
}
