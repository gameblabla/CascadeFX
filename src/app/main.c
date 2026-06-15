#include "cfx_app.h"

int main(void)
{
    CfxApp app;
    cfx_app_init(&app);
    return cfx_app_run(&app);
}
