#ifndef CFX_APP_H
#define CFX_APP_H
#include <stdint.h>

typedef struct CfxApp {
    uintptr_t opaque[8];
} CfxApp;

void cfx_app_init(CfxApp *app);
int cfx_app_run(CfxApp *app);

#endif
