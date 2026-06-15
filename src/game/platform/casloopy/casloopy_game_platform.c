#include "cfx_game_platform.h"
#include "defines.h"
#include "loopy.h"
#include "casloopy.h"
#include "gamepal.h"
#include "background.h"
#include "textures.h"
#include "title.h"
#include "cfx_loopy_timer.h"
#include "font_drawing.h"

#define CFX_VIDEO_FRAME_BYTES (SCREEN_WIDTH * SCREEN_HEIGHT)
#define CFX_LOOPY_BM_PAGE_BYTES 0x10000u
#define CFX_LOOPY_GAME_OVER_ARCADE_STATE 4
#define CFX_LOOPY_GAME_OVER_PUZZLE_STATE 5
#define CFX_LOOPY_GAME_OVER_FILL_INDEX 79

typedef struct {
    int x0;
    int y0;
    int x1;
    int y1;
} CfxVideoRect;

#define CFX_VIDEO_MAX_DIRTY_RECTS 16

typedef struct {
    int count;
    CfxVideoRect rects[CFX_VIDEO_MAX_DIRTY_RECTS];
} CfxVideoDirtyList;

static int16_t framebuffer_game[SCREEN_WIDTH * SCREEN_HEIGHT / 2];
static int cfx_loopy_back_page;
static int cfx_loopy_visible_page;
static uint8_t cfx_loopy_page_valid[2];
static CfxVideoDirtyList cfx_video_dirty_list;
static CfxVideoDirtyList cfx_video_prev_dirty_list;
static int cfx_video_prev_dirty;

uint8_t *cfx_game_framebuffer(void) { return (uint8_t *)framebuffer_game; }
const uint8_t *cfx_game_bg_game(void) { return (const uint8_t *)bg_game; }
const uint8_t *cfx_game_bg_title(void) { return (const uint8_t *)bg_title; }
const uint8_t *cfx_game_texture_atlas(void) { return texture; }

void cfx_game_memcpy(void *dest, const void *src, size_t n)
{
    int32_t *d = (int32_t *)dest;
    const int32_t *s = (const int32_t *)src;
    while (n >= 4) {
        *d++ = *s++;
        n -= 4;
    }
    uint8_t *d8 = (uint8_t *)d;
    const uint8_t *s8 = (const uint8_t *)s;
    while (n-- > 0) {
        *d8++ = *s8++;
    }
}

static void cfx_video_dirty_list_clear(CfxVideoDirtyList *list)
{
    list->count = 0;
}

static inline int cfx_video_rects_touch_or_overlap(const CfxVideoRect *a, const CfxVideoRect *b)
{
    return a->x0 <= b->x1 && a->x1 >= b->x0 &&
           a->y0 <= b->y1 && a->y1 >= b->y0;
}

static inline void cfx_video_rect_merge(CfxVideoRect *dst, const CfxVideoRect *src)
{
    if (src->x0 < dst->x0) dst->x0 = src->x0;
    if (src->y0 < dst->y0) dst->y0 = src->y0;
    if (src->x1 > dst->x1) dst->x1 = src->x1;
    if (src->y1 > dst->y1) dst->y1 = src->y1;
}

static void cfx_video_dirty_list_add(CfxVideoDirtyList *list, const CfxVideoRect *rect)
{
    for (int i = 0; i < list->count; ++i) {
        if (cfx_video_rects_touch_or_overlap(&list->rects[i], rect)) {
            cfx_video_rect_merge(&list->rects[i], rect);
            return;
        }
    }

    if (list->count < CFX_VIDEO_MAX_DIRTY_RECTS) {
        list->rects[list->count++] = *rect;
        return;
    }

    for (int i = 1; i < list->count; ++i) {
        cfx_video_rect_merge(&list->rects[0], &list->rects[i]);
    }
    cfx_video_rect_merge(&list->rects[0], rect);
    list->count = 1;
}

static void cfx_video_dirty_reset(void)
{
    cfx_video_dirty_list_clear(&cfx_video_dirty_list);
}

static void cfx_video_dirty_union_add(CfxVideoDirtyList *dst, const CfxVideoDirtyList *src)
{
    for (int i = 0; i < src->count; ++i) {
        cfx_video_dirty_list_add(dst, &src->rects[i]);
    }
}

void cfx_game_mark_dirty_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_WIDTH) x1 = SCREEN_WIDTH;
    if (y1 > SCREEN_HEIGHT) y1 = SCREEN_HEIGHT;
    if (x0 >= x1 || y0 >= y1) return;
    x0 &= ~15;
    x1 = (x1 + 15) & ~15;
    if (x1 > SCREEN_WIDTH) x1 = SCREEN_WIDTH;
    CfxVideoRect rect = {x0, y0, x1, y1};
    cfx_video_dirty_list_add(&cfx_video_dirty_list, &rect);
}

void cfx_game_mark_dirty(int index, int length)
{
    if (length <= 0) return;
    if (index < 0) {
        length += index;
        index = 0;
    }
    if (index >= CFX_VIDEO_FRAME_BYTES || length <= 0) return;
    int end = index + length;
    if (end > CFX_VIDEO_FRAME_BYTES) end = CFX_VIDEO_FRAME_BYTES;
    int y0 = index / SCREEN_WIDTH;
    int y1 = (end + SCREEN_WIDTH - 1) / SCREEN_WIDTH;
    cfx_game_mark_dirty_rect(0, y0, SCREEN_WIDTH, y1 - y0);
}

static inline volatile void *cfx_loopy_page_addr(int page)
{
    return (volatile void *)((uintptr_t)VDP_BITMAP_VRAM_8 + (page ? CFX_LOOPY_BM_PAGE_BYTES : 0u));
}

static inline uint32_t cfx_loopy_irq_save(void)
{
    uint32_t sr;
    __asm__ __volatile__("stc sr,%0" : "=r" (sr));
    maskInterrupts(15);
    return sr;
}

static inline void cfx_loopy_irq_restore(uint32_t sr)
{
    __asm__ __volatile__("ldc %0,sr" : : "r" (sr));
}

static void cfx_loopy_copy_span(volatile void *dst_base, const void *src_base, int index, int length)
{
    const uint16_t *src = (const uint16_t *)((const uint8_t *)src_base + index);
    volatile uint16_t *dst = (volatile uint16_t *)((uintptr_t)dst_base + (uintptr_t)index);
    int halfwords = length >> 1;

    while (halfwords >= 16) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        dst[4] = src[4]; dst[5] = src[5]; dst[6] = src[6]; dst[7] = src[7];
        dst[8] = src[8]; dst[9] = src[9]; dst[10] = src[10]; dst[11] = src[11];
        dst[12] = src[12]; dst[13] = src[13]; dst[14] = src[14]; dst[15] = src[15];
        dst += 16;
        src += 16;
        halfwords -= 16;
    }
    while (halfwords-- > 0) *dst++ = *src++;

    if (length & 1) {
        ((volatile uint8_t *)dst_base)[index + length - 1] = ((const uint8_t *)src_base)[index + length - 1];
    }
}

static void cfx_loopy_copy_rect_unmasked(volatile void *dst_base, const CfxVideoRect *r)
{
    if (r->x0 >= r->x1 || r->y0 >= r->y1) return;
    if (r->x0 == 0 && r->x1 == SCREEN_WIDTH) {
        cfx_loopy_copy_span(dst_base, cfx_game_framebuffer(), r->y0 * SCREEN_WIDTH, (r->y1 - r->y0) * SCREEN_WIDTH);
        return;
    }
    int width = r->x1 - r->x0;
    for (int y = r->y0; y < r->y1; ++y) {
        cfx_loopy_copy_span(dst_base, cfx_game_framebuffer(), y * SCREEN_WIDTH + r->x0, width);
    }
}

static void cfx_loopy_copy_rect(volatile void *dst_base, const CfxVideoRect *r)
{
    uint32_t sr = cfx_loopy_irq_save();
    cfx_loopy_copy_rect_unmasked(dst_base, r);
    cfx_loopy_irq_restore(sr);
}

static void cfx_loopy_clear_bitmap_pages(void)
{
    volatile uint16_t *dst = (volatile uint16_t *)VDP_BITMAP_VRAM_8;
    int halfwords = (int)((CFX_LOOPY_BM_PAGE_BYTES * 2u) >> 1);
    uint32_t sr = cfx_loopy_irq_save();
    while (halfwords-- > 0) {
        *dst++ = 0;
    }
    cfx_loopy_irq_restore(sr);
}

static inline int cfx_video_rect_is_full(const CfxVideoRect *r)
{
    return r->x0 <= 0 && r->y0 <= 0 && r->x1 >= SCREEN_WIDTH && r->y1 >= SCREEN_HEIGHT;
}

static int cfx_video_dirty_list_has_full(const CfxVideoDirtyList *list)
{
    for (int i = 0; i < list->count; ++i) {
        if (cfx_video_rect_is_full(&list->rects[i])) return 1;
    }
    return 0;
}


static void cfx_loopy_fill_framebuffer(uint8_t color)
{
    uint16_t pair = (uint16_t)(((uint16_t)color << 8) | color);
    uint16_t *dst = (uint16_t *)framebuffer_game;
    int words = SCREEN_WIDTH * SCREEN_HEIGHT / 2;
    while (words-- > 0) {
        *dst++ = pair;
    }
}

static int cfx_loopy_text_width_px(const char *s)
{
    int w = 0;
    while (*s) {
        ++w;
        ++s;
    }
    return w * 8;
}

static uint8_t cfx_loopy_gameover_menu_font_row(char ch, int row)
{
    /*
       Same 8x8 menu font glyphs used by font_menudata.h, but encoded as
       literal row returns for the game-over strings.  This keeps the
       game-over prompt immune to the mutable Loopy font-data corruption that
       can appear after a long gameplay run, while preserving the title/menu
       font appearance.
    */
    if ((uint32_t)row >= 8u) return 0;
    switch (ch) {
    case 'A':
        switch (row) { case 0: return 0x20; case 1: return 0x50; case 2: return 0x88; case 3: return 0x88; case 4: return 0xF8; case 5: return 0x88; case 6: return 0x88; default: return 0x00; }
    case 'C':
        switch (row) { case 0: return 0x70; case 1: return 0x88; case 2: return 0x80; case 3: return 0x80; case 4: return 0x80; case 5: return 0x88; case 6: return 0x70; default: return 0x00; }
    case 'E':
        switch (row) { case 0: return 0xF8; case 1: return 0x80; case 2: return 0x80; case 3: return 0xE0; case 4: return 0x80; case 5: return 0x80; case 6: return 0xF8; default: return 0x00; }
    case 'G':
        switch (row) { case 0: return 0x70; case 1: return 0x88; case 2: return 0x80; case 3: return 0x80; case 4: return 0x98; case 5: return 0x88; case 6: return 0x78; default: return 0x00; }
    case 'I':
        switch (row) { case 0: return 0xE0; case 1: return 0x40; case 2: return 0x40; case 3: return 0x40; case 4: return 0x40; case 5: return 0x40; case 6: return 0xE0; default: return 0x00; }
    case 'M':
        switch (row) { case 0: return 0x82; case 1: return 0xC6; case 2: return 0xAA; case 3: return 0x92; case 4: return 0x82; case 5: return 0x82; case 6: return 0x82; default: return 0x00; }
    case 'N':
        switch (row) { case 0: return 0x84; case 1: return 0xC4; case 2: return 0xA4; case 3: return 0x94; case 4: return 0x8C; case 5: return 0x84; case 6: return 0x84; default: return 0x00; }
    case 'O':
        switch (row) { case 0: return 0x70; case 1: return 0x88; case 2: return 0x88; case 3: return 0x88; case 4: return 0x88; case 5: return 0x88; case 6: return 0x70; default: return 0x00; }
    case 'R':
        switch (row) { case 0: return 0xF0; case 1: return 0x88; case 2: return 0x88; case 3: return 0xF0; case 4: return 0xA0; case 5: return 0x90; case 6: return 0x88; default: return 0x00; }
    case 'S':
        switch (row) { case 0: return 0x70; case 1: return 0x88; case 2: return 0x80; case 3: return 0x70; case 4: return 0x08; case 5: return 0x88; case 6: return 0x70; default: return 0x00; }
    case 'T':
        switch (row) { case 0: return 0xF8; case 1: return 0x20; case 2: return 0x20; case 3: return 0x20; case 4: return 0x20; case 5: return 0x20; case 6: return 0x20; default: return 0x00; }
    case 'U':
        switch (row) { case 0: return 0x88; case 1: return 0x88; case 2: return 0x88; case 3: return 0x88; case 4: return 0x88; case 5: return 0x88; case 6: return 0x70; default: return 0x00; }
    case 'V':
        switch (row) { case 0: return 0x88; case 1: return 0x88; case 2: return 0x88; case 3: return 0x50; case 4: return 0x50; case 5: return 0x20; case 6: return 0x20; default: return 0x00; }
    default:
        return 0x00;
    }
}

static int cfx_loopy_gameover_menu_font_pixel(char ch, int x, int y)
{
    if ((uint32_t)x >= 8u || (uint32_t)y >= 8u) return 0;
    return (cfx_loopy_gameover_menu_font_row(ch, y) & (uint8_t)(0x80u >> x)) != 0;
}

static int cfx_loopy_gameover_menu_font_outline_pixel(char ch, int x, int y)
{
    if (cfx_loopy_gameover_menu_font_pixel(ch, x, y)) return 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            if (cfx_loopy_gameover_menu_font_pixel(ch, x + dx, y + dy)) return 1;
        }
    }
    return 0;
}

static void cfx_loopy_draw_gameover_menu_font_string(const char *s, int x, int y)
{
    uint8_t *dst = (uint8_t *)framebuffer_game;
    while (*s) {
        char ch = *s++;
        for (int py = 0; py < 8; ++py) {
            int sy = y + py;
            if ((uint32_t)sy >= SCREEN_HEIGHT) continue;
            for (int px = 0; px < 8; ++px) {
                int sx = x + px;
                if ((uint32_t)sx >= SCREEN_WIDTH) continue;
                if (cfx_loopy_gameover_menu_font_pixel(ch, px, py)) {
                    dst[sy * SCREEN_WIDTH + sx] = 255;
                } else if (cfx_loopy_gameover_menu_font_outline_pixel(ch, px, py)) {
                    dst[sy * SCREEN_WIDTH + sx] = 170;
                }
            }
        }
        x += 8;
    }
}

static void cfx_loopy_print_centered_gameover_menu_font(const char *s, int y)
{
    int x = (SCREEN_WIDTH - cfx_loopy_text_width_px(s)) >> 1;
    if (x < 0) x = 0;
    cfx_loopy_draw_gameover_menu_font_string(s, x, y);
}

static void cfx_loopy_draw_game_over_screen(void)
{
    cfx_loopy_fill_framebuffer(CFX_LOOPY_GAME_OVER_FILL_INDEX);
    cfx_loopy_print_centered_gameover_menu_font("GAME OVER", 104);
    cfx_loopy_print_centered_gameover_menu_font("START TO CONTINUE", 128);
}

static void cfx_loopy_invalidate_pages(void)
{
    cfx_loopy_page_valid[0] = 0;
    cfx_loopy_page_valid[1] = 0;
    cfx_video_prev_dirty = 0;
    cfx_video_dirty_list_clear(&cfx_video_prev_dirty_list);
    cfx_video_dirty_reset();
    cfx_game_mark_dirty(0, CFX_VIDEO_FRAME_BYTES);
}

static void cfx_video_pageflip_init(void)
{
    cfx_loopy_visible_page = 0;
    cfx_loopy_back_page = 1;
    VDP_BM_MEM_CTRL = 0x01;
    VDP_BMn_SCROLLY[0] = 0;
    cfx_loopy_invalidate_pages();
}

static void cfx_loopy_debug_fps_overlay(void)
{
    enum { FPS_X = SCREEN_WIDTH - 56, FPS_Y = 4, FPS_W = 54, FPS_H = 10 };
    static int32_t last_ms = 0;
    static int frames = 0;
    static int shown_fps = 0;
    int32_t now = cfx_loopy_get_ticks_ms();

    if (last_ms == 0) {
        last_ms = now;
    }

    frames++;
    int32_t elapsed = now - last_ms;
    if (elapsed >= 1000) {
        int measured = (int)(((int32_t)frames * 1000 + (elapsed >> 1)) / elapsed);
        if (measured < 0) measured = 0;
        if (measured > 99) measured = 99;
        shown_fps = measured;
        frames = 0;
        last_ms = now;
    }

    char text[7];
    text[0] = 'F';
    text[1] = 'P';
    text[2] = 'S';
    text[3] = ' ';
    text[4] = (shown_fps >= 10) ? (char)('0' + (shown_fps / 10)) : ' ';
    text[5] = (char)('0' + (shown_fps % 10));
    text[6] = '\0';

    cfx_game_mark_dirty_rect(FPS_X - 1, FPS_Y - 1, FPS_W, FPS_H);
    print_string(text, 255, 170, FPS_X, FPS_Y, (uint8_t *)framebuffer_game);
}

void cfx_game_present_immediate(void)
{
    cfx_loopy_debug_fps_overlay();

    const int target_page = cfx_loopy_back_page;
    const int dirty_full = cfx_video_dirty_list_has_full(&cfx_video_dirty_list);
    const int target_invalid = !cfx_loopy_page_valid[target_page];
    const int need_upload = target_invalid || cfx_video_dirty_list.count > 0;

    if (need_upload) {
        volatile void *dst_page = cfx_loopy_page_addr(target_page);

        if (target_invalid || dirty_full) {
            const CfxVideoRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
            cfx_loopy_copy_rect(dst_page, &full);
        } else {
            CfxVideoDirtyList upload_list;
            cfx_video_dirty_list_clear(&upload_list);
            if (cfx_video_prev_dirty) {
                cfx_video_dirty_union_add(&upload_list, &cfx_video_prev_dirty_list);
            }
            cfx_video_dirty_union_add(&upload_list, &cfx_video_dirty_list);
            for (int i = 0; i < upload_list.count; ++i) {
                cfx_loopy_copy_rect(dst_page, &upload_list.rects[i]);
            }
        }

        cfx_loopy_page_valid[target_page] = 1;

        /* Copy to the non-visible bitmap page first, then switch the bitmap
           scroll during vblank.  This keeps Loopy's bitmap page flipping model
           explicit and avoids register flips while the current frame is being
           scanned out. */
        BiosVsync();
        VDP_BMn_SCROLLY[0] = target_page ? 256 : 0;
        cfx_loopy_visible_page = target_page;
        cfx_loopy_back_page = target_page ^ 1;

        if (dirty_full) {
            cfx_loopy_page_valid[cfx_loopy_back_page] = 0;
        }
        cfx_video_prev_dirty = cfx_video_dirty_list.count > 0 && !dirty_full;
        if (cfx_video_prev_dirty) {
            cfx_video_prev_dirty_list = cfx_video_dirty_list;
        } else {
            cfx_video_dirty_list_clear(&cfx_video_prev_dirty_list);
        }
        cfx_video_dirty_reset();
        return;
    }

    BiosVsync();
}

void cfx_game_mark_menu_dirty(int x, int y, int w, int h) { cfx_game_mark_dirty_rect(x, y, w, h); }
void cfx_game_prepare_dirty_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void cfx_game_upload_rect_pixels(const uint8_t *src, int x, int y, int w, int h)
{
    (void)src;
    cfx_game_mark_dirty_rect(x, y, w, h);
}
void cfx_game_begin_offscreen_render(void) {}
void cfx_game_end_offscreen_render(void) {}
void cfx_game_palette_empty(void) { Empty_Palette(); }
void cfx_game_fade_in(void) { fadeInPalette(gamepal, 256); }
void cfx_game_fade_out(void) { fadeOutPalette(gamepal, 256); }
void cfx_game_play_sfx(int effect_id) { (void)effect_id; }
void cfx_game_adjust_undraw_min_x(DEFAULT_INT *min_x) { *min_x -= 1; }
void cfx_game_platform_state_enter(int state_id)
{
    if (state_id == CFX_LOOPY_GAME_OVER_ARCADE_STATE ||
        state_id == CFX_LOOPY_GAME_OVER_PUZZLE_STATE) {
        /* The game-over screen is a bitmap-index screen, so it must not inherit
           a black palette from a preceding transition.  Restore the game/menu
           palette before exposing the red screen. */
        SetPalette(gamepal, 256);
        cfx_loopy_draw_game_over_screen();
    }
    cfx_loopy_invalidate_pages();
}

int cfx_game_platform_init(void)
{
    maskInterrupts(0);
    VDP_MODE = 0b00010010;
    VDP_DISPMODE = 0x00;
    VDP_BM_CTRL = 0x0001;
    VDP_BACKDROP_A = color(0, 0, 0);
    VDP_BACKDROP_B = color(0, 0, 31);
    VDP_COLORPRIO = 0b01000000;
    VDP_LAYER_CTRL = 0b1010101001000110;
    VDP_OBJ_CTRL = 0b000000100000000;
    VDP_BG_CTRL = 0b0000000000001111;
    VDP_BMn_SCREENX[0] = 0;
    VDP_BMn_SCREENY[0] = 0;
    VDP_BMn_SCROLLY[0] = 0;
    VDP_BMn_WIDTH[0] = SCREEN_WIDTH - 1;
    VDP_BMn_HEIGHT[0] = SCREEN_HEIGHT - 1;
    SetPalette(gamepal, 256);
    cfx_loopy_clear_bitmap_pages();
    cfx_video_pageflip_init();
    cfx_loopy_timer_init();
    return 0;
}
