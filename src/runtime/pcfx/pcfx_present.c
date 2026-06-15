#include "cfx_present.h"
#include "cfx_game_platform.h"
#include "common.h"
#include "pcfx.h"

char *myitoa(DEFAULT_INT value);
extern int nframe;

void cfx_runtime_present_frame(int32_t current_tick_ms)
{
    (void)current_tick_ms;
    cfx_game_present_immediate();
    ++nframe;
    #ifdef DEBUGFPS
    pcfx_vdc_debug_fps_update(getFps());
    #endif
}
