#include "command_input.h"
#include "input.h"
#include <stdio.h>
#include <string.h>

static uint16_t parse_buttons(const char *s)
{
    uint16_t b = 0;
    if (strstr(s, "left")) b |= CFX_INPUT_LEFT;
    if (strstr(s, "right")) b |= CFX_INPUT_RIGHT;
    if (strstr(s, "up")) b |= CFX_INPUT_UP;
    if (strstr(s, "down")) b |= CFX_INPUT_DOWN;
    if (strstr(s, "a")) b |= CFX_INPUT_A;
    if (strstr(s, "b")) b |= CFX_INPUT_B;
    if (strstr(s, "start")) b |= CFX_INPUT_START;
    if (strstr(s, "hard") || strstr(s, "drop")) b |= CFX_INPUT_HARD_DROP;
    return b;
}

int cfx_headless_command_for_frame(const char *script, int frame, CfxInputState *out)
{
    memset(out, 0, sizeof(*out));
    if (!script) return 0;
    FILE *f = fopen(script, "r");
    if (!f) return -1;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        int a = 0, b = 0;
        char buttons[96] = {0};
        if (sscanf(line, "%d %d %95[^\n]", &a, &b, buttons) == 3 && frame >= a && frame <= b) {
            out->held = parse_buttons(buttons);
        }
    }
    fclose(f);
    return 0;
}
