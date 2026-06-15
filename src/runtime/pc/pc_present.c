#include "cfx_present.h"
#include <SDL/SDL.h>

extern SDL_Surface *screen;
extern const float real_FPS;

void cfx_runtime_present_frame(int32_t current_tick_ms)
{
    int32_t elapsed = (int32_t)SDL_GetTicks() - current_tick_ms;
    if (real_FPS > elapsed) {
        SDL_Delay((Uint32)(real_FPS - elapsed));
    }
    SDL_Flip(screen);
}
