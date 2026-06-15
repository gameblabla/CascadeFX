#include "input_port.h"
#include "pcfx.h"
#include "common.h"

static DEFAULT_INT oldpaddata;
static DEFAULT_INT paddata;

static uint16_t cfx_pcfx_buttons_from_raw(DEFAULT_INT pad)
{
    uint16_t held = 0;
    if (pad & (1 << 11)) held |= CFX_INPUT_LEFT;
    if (pad & (1 << 9))  held |= CFX_INPUT_RIGHT;
    if (pad & (1 << 8))  held |= CFX_INPUT_UP;
    if (pad & (1 << 10)) held |= CFX_INPUT_DOWN;
    if (pad & (1 << 0))  held |= CFX_INPUT_A;
    if (pad & (1 << 1))  held |= CFX_INPUT_B;
    if (pad & (1 << 2))  held |= CFX_INPUT_HARD_DROP; /* PC-FX button III: instant hard drop. */
    if (pad & (1 << 7))  held |= CFX_INPUT_START;
    return held;
}

CfxInputFrame cfx_input_poll_game_frame(CfxPhysicsClock *drop_clock, int32_t drop_interval)
{
    CfxInputFrame out;
    out.now_ms = getTicks();
    out.drop_due = cfx_physics_drop_ready(drop_clock, out.now_ms, drop_interval);
    out.running = 1;

    oldpaddata = paddata;
    (void)eris_pad_type(0);
    paddata = eris_pad_read(0);

    out.held = cfx_pcfx_buttons_from_raw(paddata);
    out.pressed = (uint16_t)(out.held & ~cfx_pcfx_buttons_from_raw(oldpaddata));

    cfx_input_clear_game_buttons();

    if (out.pressed & CFX_INPUT_UP) up_button = 1;
    else if (out.pressed & CFX_INPUT_DOWN) down_button = 1;

    if (out.pressed & CFX_INPUT_LEFT) left_button = 1;
    else if (out.pressed & CFX_INPUT_RIGHT) right_button = 1;

    if (out.pressed & CFX_INPUT_A) a_button = 1;
    if (out.pressed & CFX_INPUT_B) b_button = 1;
    if (out.pressed & CFX_INPUT_START) start_button = 1;
    return out;
}
