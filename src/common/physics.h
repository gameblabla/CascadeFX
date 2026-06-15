#ifndef CFX_PHYSICS_H
#define CFX_PHYSICS_H

#include <stdint.h>

#define CFX_PHYSICS_CLOCK_STORAGE_WORDS 2
#define CFX_PHYSICS_MAX_DROP_STEPS_PER_FRAME 8

typedef struct CfxPhysicsClock {
    int32_t opaque[CFX_PHYSICS_CLOCK_STORAGE_WORDS];
} CfxPhysicsClock;

void cfx_physics_clock_reset(CfxPhysicsClock *clock, int32_t now_ms);
int32_t cfx_physics_clock_elapsed(CfxPhysicsClock *clock, int32_t now_ms);
int32_t cfx_physics_drop_ready(CfxPhysicsClock *clock, int32_t now_ms, int32_t interval_ms);
void cfx_physics_drop_force(CfxPhysicsClock *clock, int32_t interval_ms);

#endif
