#include <stdint.h>

extern uint32_t __bss_link_start __asm("__bss_link_start");
extern uint32_t __bss_link_end __asm("__bss_link_end");

extern uint32_t __ctors_link_start __asm("__ctors_link_start");
extern uint32_t __ctors_link_end __asm("__ctors_link_end");

extern uint32_t __data_link_start __asm("__data_link_start");
extern uint32_t __data_link_end __asm("__data_link_end");
extern uint32_t __data_load_start __asm("__data_load_start");

/* ORAM data symbols */
extern uint32_t __oram_load_start __asm("__oram_load_start");
extern uint32_t __oram_start __asm("__oram_start");
extern uint32_t __oram_end __asm("__oram_end");

typedef void(init_t)(void);

void crt_init(void)
{
    uint32_t *start;
    uint32_t *end;
    uint32_t *load;

    /* Initialize .bss */
    start = &__bss_link_start;
    end = &__bss_link_end;
    while (start < end) {
        *start++ = 0;
    }

    /* Initialize .data */
    start = &__data_link_start;
    end = &__data_link_end;
    load = &__data_load_start;
    while (start < end) {
        *start++ = *load++;
    }

    /* Initialize ORAM */
    start = &__oram_start;
    end = &__oram_end;
    load = &__oram_load_start;
    while (start < end) {
        *start++ = *load++;
    }

    /* Call constructors */
    start = &__ctors_link_start;
    end = &__ctors_link_end;
    while (start < end) {
        ((init_t*)(*start++))();
    }
}
