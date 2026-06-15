#include "cfx_app.h"
#include <string.h>

int cfx_game_run(void);

void cfx_app_init(CfxApp *app)
{
    memset(app, 0, sizeof(*app));
}

int cfx_app_run(CfxApp *app)
{
    (void)app;
    return cfx_game_run();
}
