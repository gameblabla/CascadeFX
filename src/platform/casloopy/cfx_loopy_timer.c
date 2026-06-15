#include "loopy.h"
#include "cfx_loopy_timer.h"

/* 1 kHz monotonic clock for gameplay physics.
   Use ITU0 rather than the BSC refresh timer: the Loopy BIOS configures BSC
   refresh for DRAM service, so changing RTCOR/RTCSR for a millisecond tick can
   destabilize real hardware. ITU0 is an on-chip SH7021 timer with its own
   vector and does not consume VDP time. */
#define CFX_LOOPY_CPU_HZ       16000000u
#define CFX_LOOPY_TICK_HZ      1000u
#define CFX_LOOPY_GRA0_TICKS   (CFX_LOOPY_CPU_HZ / CFX_LOOPY_TICK_HZ)

static volatile int32_t cfx_loopy_ticks_ms;

int32_t cfx_loopy_get_ticks_ms(void)
{
    return cfx_loopy_ticks_ms;
}

void cfx_loopy_timer_irq(void)
{
    cfx_loopy_ticks_ms++;
    /* Clear ITU0 IMFA.  SH7021 timer status flags use read/modify/write-zero
       semantics; keep any other status bits set while clearing bit 0. */
    TSR0 = (uint8_t)(TSR0 & (uint8_t)~0x01u);
}

void cfx_loopy_timer_init(void)
{
    uint32_t sr;

    __asm__ __volatile__("stc sr,%0" : "=r" (sr));
    maskInterrupts(15);

    /* Stop ITU0 while programming it. */
    TSTR = (uint8_t)(TSTR & (uint8_t)~0x01u);
    cfx_loopy_ticks_ms = 0;

    TCNT0 = 0;
    GRA0 = (uint16_t)CFX_LOOPY_GRA0_TICKS;
    GRB0 = 0xffffu;

    /* Internal clock /1, clear TCNT on GRA compare. */
    ITU_TCR0 = 0x20u;
    TIOR0 = 0x00u;

    /* Clear stale flags, then enable IMIA. */
    TSR0 = 0x00u;
    TIER0 = 0x01u;

    /* IPRC controls DMAC0, DMAC2, ITU0 and ITU1.  Give ITU0 a nonzero
       priority without disturbing the other modules' priorities. */
    IPRC = (uint16_t)((IPRC & 0xff0fu) | 0x00f0u);

    TSTR = (uint8_t)(TSTR | 0x01u);
    __asm__ __volatile__("ldc %0,sr" : : "r" (sr));
}
