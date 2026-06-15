#include "input_port.h"

static int32_t cd32_tick;

CfxInputFrame cfx_input_poll_game_frame(CfxPhysicsClock *drop_clock, int32_t drop_interval)
{
    CfxInputFrame out;
    cd32_tick += 16;
    out.now_ms = cd32_tick;
    out.drop_due = cfx_physics_drop_ready(drop_clock, out.now_ms, drop_interval);
    out.running = 1;
    out.held = 0;
    out.pressed = 0;
    cfx_input_clear_game_buttons();
    return out;
}
