#include "cfx_platform.h"
#include <string.h>

typedef struct {
    CfxInputState state;
} CfxHeadlessInput;

CfxInput *cfx_input_create(void *storage, size_t storage_bytes)
{
    if (storage_bytes < sizeof(CfxHeadlessInput)) return 0;
    memset(storage, 0, sizeof(CfxHeadlessInput));
    return (CfxInput *)storage;
}

void cfx_input_poll(CfxInput *input, CfxInputState *out_state)
{
    CfxHeadlessInput *in = (CfxHeadlessInput *)input;
    *out_state = in->state;
}
