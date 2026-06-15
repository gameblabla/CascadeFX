#include "input_port.h"
#include "loopy.h"
#include "casloopy.h"
#include "common.h"
#include "defines.h"
#include "cfx_loopy_timer.h"

static int32_t oldpad1;
static int32_t oldpad0;
static int32_t newpad1;
static int32_t newpad0;

static uint16_t cfx_loopy_buttons_from_raw(int32_t pad0, int32_t pad1)
{
    uint16_t held = 0;
    if (pad1 & P1_LEFT)  held |= CFX_INPUT_LEFT;
    if (pad1 & P1_RIGHT) held |= CFX_INPUT_RIGHT;
    if (pad1 & P1_UP)    held |= CFX_INPUT_UP;
    if (pad1 & P1_DOWN)  held |= CFX_INPUT_DOWN;
    if (pad0 & P1_A)     held |= CFX_INPUT_A;
    if (pad0 & P1_B)     held |= CFX_INPUT_B;
    if (pad0 & P1_C)     held |= CFX_INPUT_B;
    if (pad0 & P1_START) held |= CFX_INPUT_START;
    return held;
}

CfxInputFrame cfx_input_poll_game_frame(CfxPhysicsClock *drop_clock, int32_t drop_interval)
{
    CfxInputFrame out;
    out.now_ms = cfx_loopy_get_ticks_ms();
    out.drop_due = cfx_physics_drop_ready(drop_clock, out.now_ms, drop_interval);
    out.running = 1;

    oldpad1 = newpad1;
    oldpad0 = newpad0;
    newpad1 = IO_CONTROLLER1;
    newpad0 = IO_CONTROLLER0;

    out.held = cfx_loopy_buttons_from_raw(newpad0, newpad1);
    out.pressed = (uint16_t)(out.held & ~cfx_loopy_buttons_from_raw(oldpad0, oldpad1));

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
