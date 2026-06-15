#include "cfx_present.h"
#include "cfx_game_platform.h"

void cfx_runtime_present_frame(int32_t current_tick_ms)
{
    (void)current_tick_ms;
    cfx_game_present_immediate();
}
