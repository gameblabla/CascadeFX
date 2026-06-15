#include "input_port.h"
#include <SDL/SDL.h>

static uint16_t old_held;

static uint16_t cfx_pc_buttons_from_keys(const Uint8 *keys)
{
    uint16_t held = 0;
    if (keys[SDLK_LEFT])  held |= CFX_INPUT_LEFT;
    if (keys[SDLK_RIGHT]) held |= CFX_INPUT_RIGHT;
    if (keys[SDLK_UP])    held |= CFX_INPUT_UP;
    if (keys[SDLK_DOWN])  held |= CFX_INPUT_DOWN;
    if (keys[SDLK_SPACE]) held |= CFX_INPUT_A;
    if (keys[SDLK_l])     held |= CFX_INPUT_HARD_DROP;
    if (keys[SDLK_j])     held |= CFX_INPUT_B;
    if (keys[SDLK_k])     held |= CFX_INPUT_B;
    if (keys[SDLK_RETURN]) held |= CFX_INPUT_START;
    return held;
}

CfxInputFrame cfx_input_poll_game_frame(CfxPhysicsClock *drop_clock, int32_t drop_interval)
{
    CfxInputFrame out;
    out.now_ms = (int32_t)SDL_GetTicks();
    out.drop_due = cfx_physics_drop_ready(drop_clock, out.now_ms, drop_interval);
    out.running = 1;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                out.running = 0;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    out.running = 0;
                }
                break;
            default:
                break;
        }
    }

    Uint8 *keys = SDL_GetKeyState(0);
    out.held = cfx_pc_buttons_from_keys(keys);
    out.pressed = (uint16_t)(out.held & ~old_held);
    old_held = out.held;

    cfx_input_clear_game_buttons();
    if (out.pressed & CFX_INPUT_UP) up_button = 1;
    if (out.pressed & CFX_INPUT_DOWN) down_button = 1;
    if (out.pressed & CFX_INPUT_LEFT) left_button = 1;
    if (out.pressed & CFX_INPUT_RIGHT) right_button = 1;
    if (out.pressed & CFX_INPUT_A) a_button = 1;
    if (out.pressed & CFX_INPUT_B) b_button = 1;
    if (out.pressed & CFX_INPUT_START) start_button = 1;
    return out;
}
