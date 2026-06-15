#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "loopy.h"
#include "casloopy.h"
#define PALETTE_SIZE 256 // Assuming a palette of 256 colors
#define FADE_STEPS 31 // Define the number of steps for the fade effect

volatile void (*BiosVsync)(void) = (void (*)(void))0x6A5A;

static uint16_t cfx_loopy_current_palette[PALETTE_SIZE];
static uint8_t cfx_loopy_palette_valid;

static inline uint16_t cfx_loopy_dim_rgb555(uint16_t c, int step)
{
    uint16_t r = (uint16_t)((c >> 10) & 0x1F);
    uint16_t g = (uint16_t)((c >> 5) & 0x1F);
    uint16_t b = (uint16_t)(c & 0x1F);
    r = (uint16_t)((r * step) / FADE_STEPS);
    g = (uint16_t)((g * step) / FADE_STEPS);
    b = (uint16_t)((b * step) / FADE_STEPS);
    return (uint16_t)((r << 10) | (g << 5) | b);
}

static void cfx_loopy_apply_palette_dimmed(const uint16_t *pal, int colors, int step)
{
    if (colors <= 0 || colors > PALETTE_SIZE) colors = PALETTE_SIZE;
    for (int i = 0; i < colors; ++i) {
        VDP_PALETTE[i] = cfx_loopy_dim_rgb555(pal[i], step);
    }
    for (int i = colors; i < PALETTE_SIZE; ++i) {
        VDP_PALETTE[i] = 0;
    }
}

void Empty_Palette()
{
    BiosVsync();
	for(unsigned int i = 0; i < 256; i++){
		VDP_PALETTE[i] = 0;
        cfx_loopy_current_palette[i] = 0;
	}	
    cfx_loopy_palette_valid = 1;
}

void SetPalette(const uint16_t* pal, int colors)
{
    if (colors <= 0 || colors > PALETTE_SIZE) colors = PALETTE_SIZE;
    BiosVsync();
    for (int i = 0; i < colors; ++i) {
        cfx_loopy_current_palette[i] = pal[i];
        VDP_PALETTE[i] = pal[i];
    }
    for (int i = colors; i < PALETTE_SIZE; ++i) {
        cfx_loopy_current_palette[i] = 0;
        VDP_PALETTE[i] = 0;
    }
    cfx_loopy_palette_valid = 1;
}

size_t strlen(const char *str) {
    const char *s = str;
    while (*s) {
        s++;
    }
    return s - str;
}

void memcpy32(void *dest, const void *src, size_t n) {
    uint32_t *d = (uint32_t*)dest;
    const uint32_t *s = (const uint32_t*)src;

    // Copy 4 bytes at a time (32 bits)
    while (n >= 4) {
        *d++ = *s++;
        n -= 4;
    }

    // Handle any remaining bytes (if the size is not a multiple of 4)
    uint8_t *d8 = (uint8_t*)d;
    const uint8_t *s8 = (const uint8_t*)s;
    while (n > 0) {
        *d8++ = *s8++;
        n--;
    }
}

void *memset(void *dest, int c, size_t n){
	unsigned char *s = dest;
    for (; n; n--, s++) *s = c;
	return dest;
}

// Function to fade in from black to the target palette.
// Loopy uses one global RGB555 palette for bitmap pixels; update it once per
// vblank so screen fades transition cleanly instead of changing mid-scan.
void fadeInPalette(const uint16_t* topal, int colors) {
    if (colors <= 0 || colors > PALETTE_SIZE) colors = PALETTE_SIZE;
    for (int step = 1; step <= FADE_STEPS; step++) {
        BiosVsync();
        cfx_loopy_apply_palette_dimmed(topal, colors, step);
    }
    for (int i = 0; i < colors; ++i) {
        cfx_loopy_current_palette[i] = topal[i];
        VDP_PALETTE[i] = topal[i];
    }
    for (int i = colors; i < PALETTE_SIZE; ++i) {
        cfx_loopy_current_palette[i] = 0;
        VDP_PALETTE[i] = 0;
    }
    cfx_loopy_palette_valid = 1;
}

// Function to fade out from the currently visible palette to black.
void fadeOutPalette(const uint16_t* frompal, int colors) {
    if (colors <= 0 || colors > PALETTE_SIZE) colors = PALETTE_SIZE;
    const uint16_t *base = cfx_loopy_palette_valid ? cfx_loopy_current_palette : frompal;
    for (int step = FADE_STEPS - 1; step >= 0; --step) {
        BiosVsync();
        cfx_loopy_apply_palette_dimmed(base, colors, step);
    }
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        cfx_loopy_current_palette[i] = 0;
        VDP_PALETTE[i] = 0;
    }
    cfx_loopy_palette_valid = 1;
}
