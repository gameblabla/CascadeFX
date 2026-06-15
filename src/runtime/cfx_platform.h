#ifndef CFX_PLATFORM_H
#define CFX_PLATFORM_H
#include <stdint.h>
#include <stddef.h>

typedef struct CfxVideo CfxVideo;
typedef struct CfxInput CfxInput;
typedef struct CfxSound CfxSound;

typedef struct {
    uint16_t held;
    uint16_t pressed;
    uint16_t released;
} CfxInputState;

typedef struct {
    int width;
    int height;
    uint8_t *pixels;
} CfxFrameBuffer8;

typedef struct {
    CfxVideo *video;
    CfxInput *input;
    CfxSound *sound;
    void *user;
} CfxPlatform;

/* Video backend: platform-specific ownership hidden behind opaque handles. */
CfxVideo *cfx_video_create(void *storage, size_t storage_bytes);
void cfx_video_destroy(CfxVideo *video);
CfxFrameBuffer8 cfx_video_framebuffer(CfxVideo *video);
void cfx_video_mark_rect(CfxVideo *video, int x, int y, int w, int h);
void cfx_video_present(CfxVideo *video);

/* Input backend: game code consumes only CfxInputState. */
CfxInput *cfx_input_create(void *storage, size_t storage_bytes);
void cfx_input_poll(CfxInput *input, CfxInputState *out_state);

/* Sound backend: soundless builds may provide no-op implementation. */
CfxSound *cfx_sound_create(void *storage, size_t storage_bytes);
void cfx_sound_play(CfxSound *sound, int effect_id);
void cfx_sound_stop_all(CfxSound *sound);

#endif
