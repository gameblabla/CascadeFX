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

void Empty_Palette()
{
	for(unsigned int i = 0; i < 256; i++){
		VDP_PALETTE[i] = 0;
	}	
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

// Function to fade in from black to the target palette
void fadeInPalette(const uint16_t* topal, int colors) {
    // Gradually fade in 'topal' over FADE_STEPS
    for (int step = 1; step <= FADE_STEPS; step++) {
        for (unsigned int i = 0; i < PALETTE_SIZE; i++) {
            // Extract RGB components from 'topal[i]' (RGB555 format)
            uint8_t redTarget = (topal[i] >> 10) & 0x1F;
            uint8_t greenTarget = (topal[i] >> 5) & 0x1F;
            uint8_t blueTarget = topal[i] & 0x1F;

            // Calculate the fade for each color component based on the current step
            uint8_t red = (redTarget * step) / FADE_STEPS;
            uint8_t green = (greenTarget * step) / FADE_STEPS;
            uint8_t blue = (blueTarget * step) / FADE_STEPS;

            // Combine components back into a single RGB555 color value
            uint16_t color = (red << 10) | (green << 5) | blue;

            // Write directly to 'VDP_PALETTE'
            VDP_PALETTE[i] = color;
        }

        // Wait for the next video frame update here
        BiosVsync();
    }
}

// Function to fade out from the source palette to black
void fadeOutPalette(const uint16_t* frompal, int colors) {
    // Gradually fade out 'frompal' to black over FADE_STEPS
    for (int step = 1; step <= FADE_STEPS; step++) {
        for (unsigned int i = 0; i < PALETTE_SIZE; i++) {
            // Extract RGB components from 'frompal[i]' (RGB555 format)
            uint8_t red = (frompal[i] >> 10) & 0x1F;
            uint8_t green = (frompal[i] >> 5) & 0x1F;
            uint8_t blue = frompal[i] & 0x1F;

            // Calculate the fade for each color component based on the current step
            uint8_t redFade = (red * (FADE_STEPS - step)) / FADE_STEPS;
            uint8_t greenFade = (green * (FADE_STEPS - step)) / FADE_STEPS;
            uint8_t blueFade = (blue * (FADE_STEPS - step)) / FADE_STEPS;

            // Combine components back into a single RGB555 color value
            uint16_t color = (redFade << 10) | (greenFade << 5) | blueFade;

            // Write directly to 'VDP_PALETTE'
            VDP_PALETTE[i] = color;
        }

        // Wait for the next video frame update here
        BiosVsync();
    }
}
