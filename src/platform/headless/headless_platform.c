#include "cfx_platform.h"
#include <string.h>

#define HEADLESS_W 320
#define HEADLESS_H 240

typedef struct {
    CfxFrameBuffer8 fb;
    uint8_t pixels[HEADLESS_W * HEADLESS_H];
    int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
} CfxHeadlessVideo;

typedef struct {
    int muted;
} CfxHeadlessSound;

CfxVideo *cfx_video_create(void *storage, size_t storage_bytes)
{
    if (storage_bytes < sizeof(CfxHeadlessVideo)) return 0;
    CfxHeadlessVideo *v = (CfxHeadlessVideo *)storage;
    memset(v, 0, sizeof(*v));
    v->fb.width = HEADLESS_W;
    v->fb.height = HEADLESS_H;
    v->fb.pixels = v->pixels;
    v->dirty_x0 = HEADLESS_W;
    v->dirty_y0 = HEADLESS_H;
    return (CfxVideo *)v;
}

void cfx_video_destroy(CfxVideo *video) { (void)video; }

CfxFrameBuffer8 cfx_video_framebuffer(CfxVideo *video)
{
    CfxHeadlessVideo *v = (CfxHeadlessVideo *)video;
    return v->fb;
}

void cfx_video_mark_rect(CfxVideo *video, int x, int y, int w, int h)
{
    CfxHeadlessVideo *v = (CfxHeadlessVideo *)video;
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > HEADLESS_W) x1 = HEADLESS_W;
    if (y1 > HEADLESS_H) y1 = HEADLESS_H;
    if (x >= x1 || y >= y1) return;
    if (x < v->dirty_x0) v->dirty_x0 = x;
    if (y < v->dirty_y0) v->dirty_y0 = y;
    if (x1 > v->dirty_x1) v->dirty_x1 = x1;
    if (y1 > v->dirty_y1) v->dirty_y1 = y1;
}

void cfx_video_present(CfxVideo *video)
{
    CfxHeadlessVideo *v = (CfxHeadlessVideo *)video;
    v->dirty_x0 = HEADLESS_W; v->dirty_y0 = HEADLESS_H; v->dirty_x1 = 0; v->dirty_y1 = 0;
}

CfxSound *cfx_sound_create(void *storage, size_t storage_bytes)
{
    if (storage_bytes < sizeof(CfxHeadlessSound)) return 0;
    memset(storage, 0, sizeof(CfxHeadlessSound));
    return (CfxSound *)storage;
}

void cfx_sound_play(CfxSound *sound, int effect_id) { (void)sound; (void)effect_id; }
void cfx_sound_stop_all(CfxSound *sound) { (void)sound; }
