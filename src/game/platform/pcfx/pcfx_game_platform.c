#include "cfx_game_platform.h"
#include "defines.h"
#include "fastking.h"
#include "pcfx.h"
#include "gamepal.h"
#include "background.h"
#include "textures.h"
#include "title.h"
#include "go_sfx.h"
#include "drop.h"
#include "selsfx.h"
#include "explosion.h"
#include "readysfx.h"
#include "font_menudata.h"
#include "font_drawing.h"

#define CFX_VIDEO_FRAME_BYTES (SCREEN_WIDTH * SCREEN_HEIGHT)

#ifndef CFX_PCFX_ENABLE_CDDA
#define CFX_PCFX_ENABLE_CDDA 0
#endif

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

int16_t framebuffer_game[SCREEN_WIDTH * SCREEN_HEIGHT / 2];
static int16_t cfx_pcfx_transparent_background[SCREEN_WIDTH * SCREEN_HEIGHT / 2];
static int cfx_pcfx_back_page;
static CfxVideoDirtyList cfx_video_dirty_list;
static CfxVideoDirtyList cfx_video_prev_dirty_list;
static int cfx_video_prev_dirty;
static CfxVideoDirtyList cfx_pcfx_page_dirty[2];
static int cfx_pcfx_prepared_page = -1;
int cfx_pcfx_current_kram_page_word_offset = 0;
static int cfx_pcfx_offscreen_render_depth = 0;
static uint32_t cfx_pcfx_board_cache_epoch = 1;
static uint32_t cfx_pcfx_page_board_epoch[2] = {0, 0};
static const uint8_t *cfx_pcfx_board_restore_src = 0;
static int cfx_pcfx_vdc_hud_last_mode = -1;
static int cfx_pcfx_vdc_hud_last_value = -1;

uint8_t *cfx_game_framebuffer(void) { return (uint8_t *)framebuffer_game; }
const uint8_t *cfx_game_bg_game(void) { return (const uint8_t *)cfx_pcfx_transparent_background; }
const uint8_t *cfx_game_bg_title(void) { return (const uint8_t *)cfx_pcfx_transparent_background; }
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

static inline int cfx_pcfx_page_word_offset(int page)
{
    return page ? (CFX_VIDEO_FRAME_BYTES / 2) : 0;
}

int cfx_pcfx_current_page_word_offset(void)
{
    cfx_pcfx_current_kram_page_word_offset = cfx_pcfx_page_word_offset(cfx_pcfx_back_page);
    return cfx_pcfx_current_kram_page_word_offset;
}

static inline __attribute__((always_inline)) void cfx_pcfx_king_seek_write_word(int word_addr)
{
    uint32_t addr = (uint32_t)word_addr | (1u << 18);
    asm volatile (
        "movea 13, r0, r10\n\t"
        "out.h r10, 0x600[r0]\n\t"
        "out.w %[addr], 0x604[r0]\n\t"
        :
        : [addr] "r" (addr)
        : "r10", "memory");
}

static void cfx_pcfx_begin_frame_prepare(void);

static inline __attribute__((always_inline)) void cfx_pcfx_king_select_data_register(void)
{
    asm volatile (
        "movea 14, r0, r10\n\t"
        "out.h r10, 0x600[r0]\n\t"
        :
        :
        : "r10", "memory");
}

static void cfx_pcfx_draw_text_white_on_page(int page, const char *str, int x, int y)
{
    const int page_word_offset = cfx_pcfx_page_word_offset(page);
    for (int row = 0; row < 8; ++row) {
        int word_addr = page_word_offset + ((y + row) * (SCREEN_WIDTH >> 1)) + (x >> 1);
        cfx_pcfx_king_seek_write_word(word_addr);
        cfx_pcfx_king_select_data_register();

        const char *p = str;
        while (*p) {
            uint8_t bits = n2DLib_font[((uint8_t)*p * 8u) + (uint8_t)row];
            for (int pair = 0; pair < 4; ++pair) {
                uint8_t left = (bits & (uint8_t)(0x80u >> (pair * 2))) ? 255u : 0u;
                uint8_t right = (bits & (uint8_t)(0x40u >> (pair * 2))) ? 255u : 0u;
                uint16_t packed = (uint16_t)(((uint16_t)left << 8) | (uint16_t)right);
                asm volatile ("out.h %0, 0x604[r0]" : : "r" (packed) : "memory");
            }
            ++p;
        }
    }
}

static void cfx_pcfx_clear_rect_on_page(int page, const CfxVideoRect *r)
{
    if (r->x0 >= r->x1 || r->y0 >= r->y1) return;

    int x0 = r->x0 & ~1;
    int x1 = (r->x1 + 1) & ~1;
    if (x0 < 0) x0 = 0;
    if (x1 > SCREEN_WIDTH) x1 = SCREEN_WIDTH;
    if (x0 >= x1) return;

    const int page_word_offset = cfx_pcfx_page_word_offset(page);
    const int width_words = (x1 - x0) >> 1;
    const int rows = r->y1 - r->y0;

    /* PC-FX foreground KRAM pages are always 256 pixels wide.  When the clear
       covers a complete 256-pixel row, stream the whole rectangle as one linear
       KRAM fill instead of reseeking KING once per row.  Partial rows use the
       dedicated 256-wide zero clear emitter. */
    if (x0 == 0 && width_words == (SCREEN_WIDTH >> 1)) {
        cfx_pcfx_king_seek_write_word(page_word_offset + (r->y0 << 7));
        king_kram_fill_words(0, rows << 7);
    } else {
        king_kram_clear_rect_256_zero(page_word_offset, x0, r->y0, width_words, rows);
    }
}

static void cfx_pcfx_clear_all_foreground_pages(void)
{
    CfxVideoRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    cfx_pcfx_clear_rect_on_page(0, &full);
    cfx_pcfx_clear_rect_on_page(1, &full);
    cfx_video_dirty_list_clear(&cfx_pcfx_page_dirty[0]);
    cfx_video_dirty_list_clear(&cfx_pcfx_page_dirty[1]);
    cfx_video_dirty_list_clear(&cfx_video_dirty_list);
    cfx_pcfx_page_board_epoch[0] = 0;
    cfx_pcfx_page_board_epoch[1] = 0;
    cfx_pcfx_prepared_page = -1;
}

void cfx_game_pcfx_draw_static_title_prompt(const char *str, int x, int y)
{
    if (!str) {
        return;
    }
    /* Keep the title prompt off the KING foreground path.  It is static text,
       so draw it once into the dual-VDC title background and leave KING for
       the moving cube only. */
    cfx_pcfx_clear_all_foreground_pages();
    pcfx_vdc_print_title_prompt(x, y, str);
    cfx_pcfx_prepared_page = -1;
}

static void cfx_pcfx_upload_rect_to_page(int page, const uint8_t *src, int x, int y, int w, int h);

static void cfx_pcfx_begin_frame_prepare(void)
{
    if (cfx_pcfx_prepared_page == cfx_pcfx_back_page) {
        return;
    }

    CfxVideoDirtyList *stale = &cfx_pcfx_page_dirty[cfx_pcfx_back_page];

    if (cfx_pcfx_board_restore_src) {
        /* Gameplay pages are persistent KING foreground buffers.  If this page
           has not yet received the current locked-board bitmap, seed it before
           drawing the active 3D piece.  Otherwise restore only this page's
           stale active-piece/HUD rectangles from the 2D board cache. */
        if (cfx_pcfx_page_board_epoch[cfx_pcfx_back_page] != cfx_pcfx_board_cache_epoch) {
            cfx_pcfx_upload_rect_to_page(cfx_pcfx_back_page, cfx_pcfx_board_restore_src,
                                         0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            cfx_pcfx_page_board_epoch[cfx_pcfx_back_page] = cfx_pcfx_board_cache_epoch;
        } else {
            for (int i = 0; i < stale->count; ++i) {
                const CfxVideoRect *r = &stale->rects[i];
                cfx_pcfx_upload_rect_to_page(cfx_pcfx_back_page, cfx_pcfx_board_restore_src,
                                             r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
            }
        }
    } else {
        for (int i = 0; i < stale->count; ++i) {
            cfx_pcfx_clear_rect_on_page(cfx_pcfx_back_page, &stale->rects[i]);
        }
    }

    cfx_video_dirty_list_clear(stale);
    cfx_pcfx_prepared_page = cfx_pcfx_back_page;
}

void cfx_game_prepare_dirty_rect(int x, int y, int w, int h)
{
    if (cfx_pcfx_offscreen_render_depth > 0 || w <= 0 || h <= 0) {
        return;
    }

    CfxVideoRect r = {x, y, x + w, y + h};
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > SCREEN_WIDTH) r.x1 = SCREEN_WIDTH;
    if (r.y1 > SCREEN_HEIGHT) r.y1 = SCREEN_HEIGHT;
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    r.x0 &= ~15;
    r.x1 = (r.x1 + 15) & ~15;
    if (r.x1 > SCREEN_WIDTH) r.x1 = SCREEN_WIDTH;

    (void)r;
    /* Direct-KING page flipping only needs to clear the stale dirty regions
       recorded for the page that just became the back page.  Clearing the
       new/current dirty rectangle here doubles the title cube cost: that area
       is already transparent unless it was part of this page's previous draw,
       in which case cfx_pcfx_begin_frame_prepare() has just cleared it. */
    cfx_pcfx_begin_frame_prepare();
}

void cfx_game_begin_offscreen_render(void)
{
    ++cfx_pcfx_offscreen_render_depth;
}

void cfx_game_end_offscreen_render(void)
{
    if (cfx_pcfx_offscreen_render_depth > 0) {
        --cfx_pcfx_offscreen_render_depth;
    }
}

static void cfx_pcfx_upload_rect_to_page(int page, const uint8_t *src, int x, int y, int w, int h)
{
    if (!src || w <= 0 || h <= 0) {
        return;
    }

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
    if (x0 >= x1) return;

    const int page_word_offset = cfx_pcfx_page_word_offset(page);
    const int row_bytes = x1 - x0;
    const int rows = y1 - y0;

    /* PC-FX is fixed at 256 pixels wide; no non-256 fallback is useful here. */
    if (x0 == 0 && row_bytes == SCREEN_WIDTH) {
        cfx_pcfx_king_seek_write_word(page_word_offset + (y0 << 7));
        king_kram_write_buffer((void *)(src + (y0 << 8)), row_bytes * rows);
    } else {
        king_kram_upload_rect_256(src, page_word_offset, x0, y0, row_bytes, rows);
    }
}

void cfx_game_upload_rect_pixels(const uint8_t *src, int x, int y, int w, int h)
{
    if (cfx_pcfx_offscreen_render_depth > 0 || !src || w <= 0 || h <= 0) {
        return;
    }

    cfx_pcfx_begin_frame_prepare();
    cfx_pcfx_upload_rect_to_page(cfx_pcfx_back_page, src, x, y, w, h);
}

void cfx_game_pcfx_upload_rect_all_pages(const uint8_t *src, int x, int y, int w, int h)
{
    if (cfx_pcfx_offscreen_render_depth > 0 || !src || w <= 0 || h <= 0) {
        return;
    }

    CfxVideoRect r = {x, y, x + w, y + h};
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > SCREEN_WIDTH) r.x1 = SCREEN_WIDTH;
    if (r.y1 > SCREEN_HEIGHT) r.y1 = SCREEN_HEIGHT;
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    r.x0 &= ~15;
    r.x1 = (r.x1 + 15) & ~15;
    if (r.x1 > SCREEN_WIDTH) r.x1 = SCREEN_WIDTH;

    cfx_pcfx_upload_rect_to_page(0, src, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    cfx_pcfx_upload_rect_to_page(1, src, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    cfx_video_dirty_list_add(&cfx_pcfx_page_dirty[0], &r);
    cfx_video_dirty_list_add(&cfx_pcfx_page_dirty[1], &r);
}

void cfx_game_pcfx_upload_static_overlay_all_pages(const uint8_t *src)
{
    if (cfx_pcfx_offscreen_render_depth > 0 || !src) {
        return;
    }

    /* Static full-screen foreground overlays are authoritative on both KING
       page-flip buffers.  Keep this path for game-over screens, where the full
       transparent foreground really is the new scene. */
    cfx_pcfx_upload_rect_to_page(0, src, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    cfx_pcfx_upload_rect_to_page(1, src, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    cfx_video_dirty_list_clear(&cfx_pcfx_page_dirty[0]);
    cfx_video_dirty_list_clear(&cfx_pcfx_page_dirty[1]);
    cfx_video_dirty_list_clear(&cfx_video_dirty_list);
    cfx_pcfx_prepared_page = -1;
}

void cfx_game_pcfx_fast_title_to_menu(int prompt_x, int prompt_y, int prompt_w, int prompt_h)
{
    /* Title and menu share the same VDC background and CDDA track.  The old
       generic state-enter path reloaded the entire title background, cleared
       both full KING pages, then uploaded a full-screen transparent overlay;
       that made RUN feel delayed even though the menu is only text.  Instead,
       clear only title-cube foreground dirt from both KING pages and restore
       only the VDC tiles occupied by the title prompt. */
    for (int page = 0; page < 2; ++page) {
        for (int i = 0; i < cfx_pcfx_page_dirty[page].count; ++i) {
            cfx_pcfx_clear_rect_on_page(page, &cfx_pcfx_page_dirty[page].rects[i]);
        }
    }

    if (cfx_video_dirty_list.count > 0) {
        for (int page = 0; page < 2; ++page) {
            for (int i = 0; i < cfx_video_dirty_list.count; ++i) {
                cfx_pcfx_clear_rect_on_page(page, &cfx_video_dirty_list.rects[i]);
            }
        }
    }

    int tx = prompt_x >> 3;
    int ty = prompt_y >> 3;
    int tw = (prompt_w + 15) >> 3;
    int th = (prompt_h + 15) >> 3;
    pcfx_vdc_restore_bg_tiles(tx, ty, tw, th);

    cfx_game_pcfx_set_board_restore_source(0);
    cfx_video_dirty_list_clear(&cfx_video_prev_dirty_list);
    cfx_video_dirty_list_clear(&cfx_video_dirty_list);
    cfx_video_dirty_list_clear(&cfx_pcfx_page_dirty[0]);
    cfx_video_dirty_list_clear(&cfx_pcfx_page_dirty[1]);
    cfx_video_prev_dirty = 0;
    cfx_pcfx_prepared_page = -1;
}

void cfx_game_pcfx_set_board_restore_source(const uint8_t *src)
{
    cfx_pcfx_board_restore_src = src;
}

void cfx_game_pcfx_board_cache_changed(void)
{
    ++cfx_pcfx_board_cache_epoch;
    if (cfx_pcfx_board_cache_epoch == 0) {
        cfx_pcfx_board_cache_epoch = 1;
    }
    cfx_pcfx_page_board_epoch[0] = 0;
    cfx_pcfx_page_board_epoch[1] = 0;
}

void cfx_game_pcfx_board_cache_rect_changed(const uint8_t *src, int x, int y, int w, int h)
{
    if (cfx_pcfx_offscreen_render_depth > 0 || !src || w <= 0 || h <= 0) {
        return;
    }

    cfx_pcfx_board_restore_src = src;
    cfx_pcfx_upload_rect_to_page(0, src, x, y, w, h);
    cfx_pcfx_upload_rect_to_page(1, src, x, y, w, h);

    /* The caller has already rebuilt the CPU-side locked-board cache and this
       helper mirrors the changed board rectangle into both persistent KING
       page-flip buffers.  Keep both page epochs current here; otherwise the
       next two frame preparations treat the pages as stale and perform full
       256x240 board-cache uploads after every line-clear/global rebuild.  The
       per-page dirty lists are intentionally left intact so stale active-piece
       and landing-marker rectangles are still restored from the new board
       cache before the next live 3D draw. */
    cfx_pcfx_page_board_epoch[0] = cfx_pcfx_board_cache_epoch;
    cfx_pcfx_page_board_epoch[1] = cfx_pcfx_board_cache_epoch;
    cfx_pcfx_prepared_page = -1;
}


void cfx_game_pcfx_board_cache_rect_changed_deferred_page(const uint8_t *src, int x, int y, int w, int h)
{
    if (cfx_pcfx_offscreen_render_depth > 0 || !src || w <= 0 || h <= 0) {
        return;
    }

    CfxVideoRect r = {x, y, x + w, y + h};
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > SCREEN_WIDTH) r.x1 = SCREEN_WIDTH;
    if (r.y1 > SCREEN_HEIGHT) r.y1 = SCREEN_HEIGHT;
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    r.x0 &= ~15;
    r.x1 = (r.x1 + 15) & ~15;
    if (r.x1 > SCREEN_WIDTH) r.x1 = SCREEN_WIDTH;
    if (r.x0 >= r.x1) return;

    cfx_pcfx_board_restore_src = src;

    /* Non-clearing piece locks usually touch only one board area.  Upload the
       changed board-cache rectangle to the page being rendered now, then mark
       the alternate page stale so its copy is restored from the CPU board cache
       when it next becomes the back page.  This removes the single-frame hitch
       caused by doing both KING page uploads in the lock frame, without allowing
       either persistent page to display stale locked-board pixels. */
    cfx_pcfx_upload_rect_to_page(cfx_pcfx_back_page, src, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    cfx_video_dirty_list_add(&cfx_pcfx_page_dirty[cfx_pcfx_back_page ^ 1], &r);
    cfx_pcfx_page_board_epoch[0] = cfx_pcfx_board_cache_epoch;
    cfx_pcfx_page_board_epoch[1] = cfx_pcfx_board_cache_epoch;
}

void cfx_game_pcfx_restore_board_cache_rect(const uint8_t *src, int x, int y, int w, int h)
{
    if (cfx_pcfx_offscreen_render_depth > 0 || !src || w <= 0 || h <= 0) {
        return;
    }

    /* A KING foreground page is persistent.  After the locked-board cache changes,
       each of the two page-flip buffers must receive the complete 2D board once;
       later movement frames upload only the active-piece restore rectangle. */
    if (cfx_pcfx_page_board_epoch[cfx_pcfx_back_page] != cfx_pcfx_board_cache_epoch) {
        cfx_game_upload_rect_pixels(src, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        cfx_pcfx_page_board_epoch[cfx_pcfx_back_page] = cfx_pcfx_board_cache_epoch;
        if (x <= 0 && y <= 0 && w >= SCREEN_WIDTH && h >= SCREEN_HEIGHT) {
            return;
        }
    }

    cfx_game_upload_rect_pixels(src, x, y, w, h);
}

static void cfx_video_pageflip_init(void)
{
    cfx_pcfx_back_page = 1;
    cfx_pcfx_current_kram_page_word_offset = cfx_pcfx_page_word_offset(cfx_pcfx_back_page);
    cfx_video_prev_dirty = 0;
    cfx_pcfx_prepared_page = -1;
    cfx_video_dirty_list_clear(&cfx_video_prev_dirty_list);
    cfx_video_dirty_list_clear(&cfx_pcfx_page_dirty[0]);
    cfx_video_dirty_list_clear(&cfx_pcfx_page_dirty[1]);
    eris_king_set_bat_cg_addr(KING_BG0, 0, 0);
    eris_king_set_bat_cg_addr(KING_BG0SUB, 0, 0);
    cfx_video_dirty_reset();
    cfx_game_mark_dirty(0, CFX_VIDEO_FRAME_BYTES);
}

void cfx_game_present_immediate(void)
{
    if (cfx_video_dirty_list.count <= 0) return;
    const int page_word_offset = cfx_pcfx_page_word_offset(cfx_pcfx_back_page);

    if (cfx_pcfx_prepared_page != cfx_pcfx_back_page) {
        cfx_pcfx_begin_frame_prepare();
        for (int i = 0; i < cfx_video_dirty_list.count; ++i) {
            cfx_pcfx_clear_rect_on_page(cfx_pcfx_back_page, &cfx_video_dirty_list.rects[i]);
        }
    }

    /* Foreground pixels are written directly by the renderer/font/UI paths.
       Present no longer scans framebuffer_game and no longer copies dirty
       spans to KING.  It only points BG0 at the freshly drawn page and records
       which regions must be cleared the next time this page becomes the back
       page. */
    eris_king_set_bat_cg_addr(KING_BG0, 0, page_word_offset / 1024);
    eris_king_set_bat_cg_addr(KING_BG0SUB, 0, page_word_offset / 1024);

    cfx_pcfx_page_dirty[cfx_pcfx_back_page] = cfx_video_dirty_list;
    cfx_pcfx_back_page ^= 1;
    cfx_pcfx_current_kram_page_word_offset = cfx_pcfx_page_word_offset(cfx_pcfx_back_page);
    cfx_pcfx_prepared_page = -1;
    cfx_video_prev_dirty = 1;
    cfx_video_prev_dirty_list = cfx_video_dirty_list;
    cfx_video_dirty_reset();
}

void cfx_game_mark_menu_dirty(int x, int y, int w, int h) { cfx_game_mark_dirty_rect(x, y, w, h); }
void cfx_game_palette_empty(void) { Empty_Palette(); }

static unsigned short cfx_pcfx_dim_yuv(unsigned short pal_entry, int step)
{
    int y = (pal_entry >> 8) & 0xff;
    int u = (pal_entry >> 4) & 0x0f;
    int v = pal_entry & 0x0f;
    if (step < 0) step = 0;
    if (step > 16) step = 16;

    /* Fade to neutral YUV black, not just zero-luma with stale chroma. */
    y = (y * step) / 16;
    u = 8 + (((u - 8) * step) / 16);
    v = 8 + (((v - 8) * step) / 16);

    if (y < 0) y = 0; else if (y > 0xff) y = 0xff;
    if (u < 0) u = 0; else if (u > 0x0f) u = 0x0f;
    if (v < 0) v = 0; else if (v > 0x0f) v = 0x0f;
    return (unsigned short)((y << 8) | (u << 4) | v);
}

static void cfx_pcfx_set_king_brightness(int step)
{
    static unsigned short dimmed[256];
    if (step < 0) step = 0;
    if (step > 16) step = 16;
    for (int i = 0; i < 256; ++i) {
        dimmed[i] = cfx_pcfx_dim_yuv(gamepal[i], step);
    }
    Upload_Palette(dimmed, 256);
}

void cfx_game_fade_in(void)
{
    /* PC-FX now uses the dual VDCs for the visible background, prompt, FPS,
       and gameplay HUD text. Fade VDC and KING together in one vsync loop. */
    for (int step = 0; step <= 16; ++step) {
        pcfx_vdc_set_brightness_current(step);
        cfx_pcfx_set_king_brightness(step);
        vsync(0);
    }
}

void cfx_game_fade_out(void)
{
    for (int step = 16; step >= 0; --step) {
        pcfx_vdc_set_brightness_current(step);
        cfx_pcfx_set_king_brightness(step);
        vsync(0);
    }
}

void cfx_game_pcfx_vdc_game_hud_reset(void)
{
    /* Top-left gameplay HUD area: restore the underlying VDC background tiles.
       Do not touch the top-right FPS overlay. */
    pcfx_vdc_restore_bg_tiles(0, 0, 10, 5);
    cfx_pcfx_vdc_hud_last_mode = -1;
    cfx_pcfx_vdc_hud_last_value = -1;
}

void cfx_game_pcfx_vdc_game_hud_update(uint8_t puzzle_mode, int value)
{
    char text[8];
    int mode = puzzle_mode ? 1 : 0;
    int n = value;
    if (n < 0) n = 0;
    if (n > 99999) n = 99999;
    if (mode == cfx_pcfx_vdc_hud_last_mode && n == cfx_pcfx_vdc_hud_last_value) {
        return;
    }
    cfx_pcfx_vdc_hud_last_mode = mode;
    cfx_pcfx_vdc_hud_last_value = n;

    if (puzzle_mode) {
        text[0] = 'L'; text[1] = 'E'; text[2] = 'F'; text[3] = 'T';
        text[4] = 0;
        pcfx_vdc_restore_bg_tiles(0, 0, 10, 5);
        pcfx_vdc_print_combo_text(1, 1, text, 6);
        text[0] = (n >= 100) ? (char)('0' + ((n / 100) % 10)) : ' ';
        text[1] = (n >= 10)  ? (char)('0' + ((n / 10) % 10)) : ' ';
        text[2] = (char)('0' + (n % 10));
        text[3] = 0;
        pcfx_vdc_print_combo_text(1, 2, text, 6);
    } else {
        pcfx_vdc_restore_bg_tiles(0, 0, 10, 5);
        int pos = 5;
        text[6] = 0;
        text[5] = (char)('0' + (n % 10)); n /= 10;
        while (pos > 0) {
            --pos;
            text[pos] = (n > 0) ? (char)('0' + (n % 10)) : ' ';
            n /= 10;
        }
        pcfx_vdc_print_combo_text(1, 1, text, 6);
    }
}

static int cfx_pcfx_text_width_px(const char *str)
{
    int len = 0;
    if (!str) return 0;
    while (str[len]) ++len;
    return len << 3;
}

static void cfx_pcfx_print_centered_menu_font(const char *str, int y)
{
    int x = (SCREEN_WIDTH - cfx_pcfx_text_width_px(str)) >> 1;
    if (x < 0) x = 0;
    print_string(str, 255, 170, x, y, (uint8_t *)framebuffer_game);
}

void cfx_game_pcfx_vdc_draw_menu(int menu_selection)
{
    /* The title/menu mode is a static dual-VDC background.  Draw the mode menu
       directly into that tilemap instead of building a KING bitmap overlay; it
       updates in a handful of BAT writes and is not affected by KING page-flip
       stale-rect clearing. */
    pcfx_vdc_print_combo_text(10, 20, "Select Mode", 16);

    switch (menu_selection) {
    default:
    case 0:
        pcfx_vdc_print_combo_text(9, 22, "> Arcade Mode", 18);
        pcfx_vdc_print_combo_text(9, 25, "  Training", 18);
        pcfx_vdc_print_combo_text(9, 27, "  Credits", 18);
        break;
    case 1:
        pcfx_vdc_print_combo_text(9, 22, "  Arcade Mode", 18);
        pcfx_vdc_print_combo_text(9, 25, "> Training", 18);
        pcfx_vdc_print_combo_text(9, 27, "  Credits", 18);
        break;
    case 2:
        pcfx_vdc_print_combo_text(9, 22, "  Arcade Mode", 18);
        pcfx_vdc_print_combo_text(9, 25, "  Training", 18);
        pcfx_vdc_print_combo_text(9, 27, "> Credits", 18);
        break;
    }
}

void cfx_game_pcfx_show_game_over_screen(void)
{
    cfx_pcfx_clear_all_foreground_pages();
    Clear_BG0(KING_BGMODE_256_PAL);
    eris_king_set_bat_cg_addr(KING_BG0, 0, 0);
    eris_king_set_bat_cg_addr(KING_BG0SUB, 0, 0);
    cfx_pcfx_back_page = 1;
    cfx_pcfx_current_kram_page_word_offset = cfx_pcfx_page_word_offset(cfx_pcfx_back_page);
    cfx_pcfx_prepared_page = -1;
    pcfx_vdc_use_game_over_red_background();

    /* Game-over text is a KING foreground overlay, drawn with the same 8x8
       CPU menu font/outline path as the mode menu.  Keep the VDC reserved for
       the solid red background and debug FPS overlay. */
    for (int i = 0; i < (SCREEN_WIDTH * SCREEN_HEIGHT / 2); ++i) {
        framebuffer_game[i] = 0;
    }
    cfx_pcfx_print_centered_menu_font("GAME OVER", 104);
    cfx_pcfx_print_centered_menu_font("RUN TO CONTINUE", 128);
    cfx_game_pcfx_upload_static_overlay_all_pages((const uint8_t *)framebuffer_game);
}

void cfx_game_play_sfx(int effect_id) { Play_PSGSample(0, effect_id, 0); }
void cfx_game_adjust_undraw_min_x(DEFAULT_INT *min_x) { *min_x -= 1; }

void cfx_game_platform_state_enter(int state_id)
{
    /* State changes may leave static title foreground or gameplay HUD pixels on
       the page that is not currently being prepared.  Clear both KING pages once
       on transitions; regular frames still use dirty face clears only. */
    cfx_pcfx_clear_all_foreground_pages();
    if (state_id != 2 && state_id != 3) {
        cfx_game_pcfx_set_board_restore_source(0);
    }

    switch (state_id) {
    case 0: /* title */
    case 1: /* menu */
    case 6: /* credits */
        pcfx_vdc_use_title_background();
#if CFX_PCFX_ENABLE_CDDA
        cd_start_track(2);
        cd_end_track(3, CDDA_LOOP);
        eris_low_cdda_set_volume(63, 63);
#endif
        break;
    case 2: /* arcade */
        pcfx_vdc_use_game_background();
        cfx_game_pcfx_vdc_game_hud_reset();
#if CFX_PCFX_ENABLE_CDDA
        eris_low_cdda_set_volume(0, 0);
        cd_start_track(3);
        cd_end_track(4, CDDA_LOOP);
        eris_low_cdda_set_volume(63 / 2, 63 / 2);
#endif
        break;
    case 3: /* puzzle */
        pcfx_vdc_use_game_background();
        cfx_game_pcfx_vdc_game_hud_reset();
#if CFX_PCFX_ENABLE_CDDA
        eris_low_cdda_set_volume(63 / 2, 63 / 2);
        cd_start_track(4);
        cd_end_track(5, CDDA_LOOP);
#endif
        break;
    case 4:
    case 5:
        cfx_game_pcfx_show_game_over_screen();
#if CFX_PCFX_ENABLE_CDDA
        eris_low_cdda_set_volume(0, 0);
#endif
        break;
    default:
        break;
    }
}

int cfx_game_platform_init(void)
{
    eris_king_init();
    eris_tetsu_init();
    eris_pad_init(0);
#if CFX_PCFX_ENABLE_CDDA
    eris_low_cdda_set_volume(63, 63);
#endif
    Set_Video(KING_BGMODE_256_PAL);
    cfx_video_pageflip_init();
    Upload_Palette(gamepal, 256);

    /* The same timer IRQ drives PSG PCM output and the millisecond
       gameplay clock. pcfx.c converts the 91-cycle sample IRQ cadence back
       into elapsed milliseconds for getTicks()/FPS. */
    initTimer(1, 91);
    Load_PSGSample_RAM(drop_sfx, 0, sizeof(drop_sfx));
    Load_PSGSample_RAM(explosion_sfx, 1, sizeof(explosion_sfx));
    Load_PSGSample_RAM(select_sfx, 2, sizeof(select_sfx));
    Load_PSGSample_RAM(ready_sfx, 3, sizeof(ready_sfx));
    Load_PSGSample_RAM(go_sfx, 4, sizeof(go_sfx));
#if CFX_PCFX_ENABLE_CDDA
    cd_pausectrl(0);
#endif
    return 0;
}
