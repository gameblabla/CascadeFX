#include "physics.h"

typedef struct {
    int32_t last_ms;
    int32_t accumulator_ms;
} CfxPhysicsClockState;

static inline CfxPhysicsClockState *clock_state(CfxPhysicsClock *clock)
{
    return (CfxPhysicsClockState *)(void *)clock->opaque;
}

void cfx_physics_clock_reset(CfxPhysicsClock *clock, int32_t now_ms)
{
    CfxPhysicsClockState *state = clock_state(clock);
    state->last_ms = now_ms;
    state->accumulator_ms = 0;
}

int32_t cfx_physics_clock_elapsed(CfxPhysicsClock *clock, int32_t now_ms)
{
    CfxPhysicsClockState *state = clock_state(clock);
    int32_t dt = now_ms - state->last_ms;
    if (dt < 0) {
        dt = 0;
    }
    if (dt > 250) {
        dt = 250;
    }
    state->last_ms = now_ms;
    state->accumulator_ms += dt;
    return dt;
}

int32_t cfx_physics_drop_ready(CfxPhysicsClock *clock, int32_t now_ms, int32_t interval_ms)
{
    CfxPhysicsClockState *state = clock_state(clock);
    int32_t due;

    cfx_physics_clock_elapsed(clock, now_ms);
    if (interval_ms < 1) {
        interval_ms = 1;
    }

    due = state->accumulator_ms / interval_ms;
    if (due <= 0) {
        return 0;
    }

    /* Treat the return value as a catch-up count, not a Boolean.  When Loopy
       or PC-FX spends too long rasterizing polygon-heavy scanlines, one render
       frame can cover multiple fall intervals.  Consume those intervals here
       so the visible fall speed remains tied to elapsed time instead of FPS.
       The cap prevents a very long pause from creating an unplayable spiral. */
    if (due > CFX_PHYSICS_MAX_DROP_STEPS_PER_FRAME) {
        due = CFX_PHYSICS_MAX_DROP_STEPS_PER_FRAME;
    }

    state->accumulator_ms -= due * interval_ms;
    return due;
}

void cfx_physics_drop_force(CfxPhysicsClock *clock, int32_t interval_ms)
{
    CfxPhysicsClockState *state = clock_state(clock);
    if (interval_ms < 1) {
        interval_ms = 1;
    }
    state->accumulator_ms = interval_ms + 1;
}
