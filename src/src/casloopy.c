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

void *memcpy(void *restrict dest, const void *restrict src, size_t n){
	unsigned char *d = dest;
	const unsigned char *s = src;
	
	for(; n; n--){
		*d++ = *s++;
	}
	return dest;
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


// Function to fade in from black to pict6pal
void fadeInPalette(const uint16_t* topal, int colors) {
	uint32_t tempPalette[PALETTE_SIZE];
	
    // Initialize tempPalette to black
    for(unsigned int i = 0; i < PALETTE_SIZE; i++) {
        tempPalette[i] = 0x000000; // Assuming RGB color format, black
    }
    
    // Gradually fade in pict6pal over FADE_STEPS
    for(int step = 1; step <= FADE_STEPS; step++) {
        for(unsigned int i = 0; i < PALETTE_SIZE; i++) {
            // Extract RGB components from pict6pal[i] (RGB555 format)
            uint8_t redTarget = (topal[i] >> 10) & 0x1F;
            uint8_t greenTarget = (topal[i] >> 5) & 0x1F;
            uint8_t blueTarget = topal[i] & 0x1F;

            // Calculate the fade for each color component based on the current step
            uint8_t red = (redTarget * step) / FADE_STEPS;
            uint8_t green = (greenTarget * step) / FADE_STEPS;
            uint8_t blue = (blueTarget * step) / FADE_STEPS;

            // Combine components back into a single RGB555 color value
            tempPalette[i] = (red << 10) | (green << 5) | blue;
        }
        
        // Copy tempPalette to VDP_PALETTE to display the current fade step
        for(unsigned int i = 0; i < PALETTE_SIZE; i++) {
            VDP_PALETTE[i] = tempPalette[i];
        }
        
        // Wait for next video frame update here
        // You need to implement this function based on your system's capabilities
		BiosVsync();
    }
}

// Function to fade out from pict6pal to black
void fadeOutPalette(const uint16_t* frompal, int colors) {
    uint32_t tempPalette[PALETTE_SIZE];
    uint32_t fpal[PALETTE_SIZE];
    memcpy32(tempPalette, frompal, PALETTE_SIZE/4);
    memcpy32(fpal, frompal, PALETTE_SIZE/4);
    
    for(unsigned int i = 0; i < PALETTE_SIZE; i++) 
    {
        // Extract RGB components from frompal[i] (RGB555 format)
        uint8_t red = (frompal[i] >> 10) & 0x1F;
        uint8_t green = (frompal[i] >> 5) & 0x1F;
        uint8_t blue = frompal[i] & 0x1F;

        // Convert RGB555 to RGB888 for processing
        tempPalette[i] = ((red * 255 / 31) << 16) | ((green * 255 / 31) << 8) | (blue * 255 / 31);
    }

    // Gradually fade out frompal to black over FADE_STEPS
    for(int step = 1; step <= FADE_STEPS; step++) 
    {
        for(unsigned int i = 0; i < PALETTE_SIZE; i++) {
            // Convert back to RGB555 components for decrement calculation
            uint8_t red = (tempPalette[i] >> 16) & 0xFF;
            uint8_t green = (tempPalette[i] >> 8) & 0xFF;
            uint8_t blue = tempPalette[i] & 0xFF;

            // Calculate the decrement for each color component based on the current step
            uint8_t redDecrement = ((red * (FADE_STEPS - step)) / FADE_STEPS) * 31 / 255;
            uint8_t greenDecrement = ((green * (FADE_STEPS - step)) / FADE_STEPS) * 31 / 255;
            uint8_t blueDecrement = ((blue * (FADE_STEPS - step)) / FADE_STEPS) * 31 / 255;

            // Combine components back into a single RGB555 color value for tempPalette
            fpal[i] = (redDecrement << 10) | (greenDecrement << 5) | blueDecrement;
        }
        
        // Copy tempPalette to VDP_PALETTE to display the current fade step
        for(unsigned int i = 0; i < PALETTE_SIZE; i++) {
            VDP_PALETTE[i] = fpal[i];
        }

       BiosVsync();
    }
}
