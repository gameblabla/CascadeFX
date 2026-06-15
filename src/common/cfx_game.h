#ifndef CFX_GAME_H
#define CFX_GAME_H
#include <stdint.h>
#include <stddef.h>
#include "cfx_platform.h"

#define CFX_GAME_STORAGE_WORDS 4096

typedef struct CfxGame {
    uintptr_t opaque[CFX_GAME_STORAGE_WORDS];
} CfxGame;

typedef struct {
    CfxPlatform platform;
    int fixed_step_ms;
} CfxGameConfig;

void cfx_game_init(CfxGame *game, const CfxGameConfig *config);
void cfx_game_reset(CfxGame *game);
void cfx_game_step(CfxGame *game, int32_t now_ms, const CfxInputState *input);
void cfx_game_render(CfxGame *game);

#endif
