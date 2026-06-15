#ifndef CFX_LOOPY_TIMER_H
#define CFX_LOOPY_TIMER_H

#include <stdint.h>

void cfx_loopy_timer_init(void);
int32_t cfx_loopy_get_ticks_ms(void);
void cfx_loopy_timer_irq(void) __attribute__((interrupt_handler));

#endif
