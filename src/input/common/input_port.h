#ifndef CFX_INPUT_PORT_H
#define CFX_INPUT_PORT_H

#include <stdint.h>
#include "common.h"
#include "physics.h"
#include "input.h"

/* Public game-facing buttons.  Platform ports translate raw controller state
   into edge-triggered game buttons for legacy menu/training code, and into
   held/pressed bitmasks for arcade DAS/ARR/IRS/IHS. */
extern DEFAULT_INT a_button;
extern DEFAULT_INT b_button;
extern DEFAULT_INT up_button;
extern DEFAULT_INT down_button;
extern DEFAULT_INT left_button;
extern DEFAULT_INT right_button;
extern DEFAULT_INT start_button;
extern DEFAULT_INT select_button;
extern DEFAULT_INT hold_down_button;

typedef struct CfxInputFrame {
    int32_t now_ms;
    int32_t drop_due;
    int running;
    uint16_t held;
    uint16_t pressed;
} CfxInputFrame;

void cfx_input_clear_game_buttons(void);
CfxInputFrame cfx_input_poll_game_frame(CfxPhysicsClock *drop_clock, int32_t drop_interval);

#define BUTTON_PRESSED(button_field) ((button_field) != 0)
#define DPAD_PRESSED(button_field) BUTTON_PRESSED(button_field)

#endif
