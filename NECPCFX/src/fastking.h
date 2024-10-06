#ifndef MY_ASM_FUNCS_H
#define MY_ASM_FUNCS_H

#include <stdint.h>
#include "pcfx.h"
void king_kram_write_buffer(void* addr, int size);
void king_kram_write_buffer_bytes(void* addr, int size);
void eris_king_set_bg0_affine_coefficient_a(int16_t x);
void eris_king_set_bg0_affine_coefficient_b(int16_t x);
void eris_king_set_bg0_affine_coefficient_c(int16_t x);
void eris_king_set_bg0_affine_coefficient_d(int16_t x);

void eris_king_set_bg_affine_center_x(int16_t x) ;
void eris_king_set_bg_affine_center_y(int16_t x) ;

#endif
