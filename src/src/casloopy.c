#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "loopy.h"
#include "casloopy.h"

#include "titlemusic.h"
#include "gamemusic.h"

#include "gamepal.h"

#define PALETTE_SIZE 256 // Assuming a palette of 256 colors
#define FADE_STEPS 31 // Define the number of steps for the fade effect

uint16_t soundState[128];

const uint8_t __attribute__((aligned(4))) track_stop[] = {
	0xFF, 
};

const void __attribute__((aligned(4))) *trackList[] = {
	(const void*)title_music,
	(const void*)gamemusic,
	(const void*)track_stop,
};

void **biosSoundState = (void **) 0x0900003C;

volatile void (*BiosVsync)(void) = (void (*)(void))0x6A5A;
volatile void (*const BiosSoundDemo)(void) = (void (*)())0x6B86;
volatile void (*const BiosInitSoundTransmission)() = (void (*)())0x613C;
volatile void (*const BiosSoundChannels)(int) = (void (*)(int))0x6AC0;
volatile void (*const BiosSoundVolume)(int, int) = (void (*)(int, int))0x6B50;
volatile void (*const BiosPlayBGM)(void*, int, int, void**) = (void (*)(void*, int, int, void**))0x61A0;

void Init_Video_Game()
{

	*biosSoundState = &soundState[0];
	
	DMAOR = 0x0001;
	IPRC = (IPRC & 0xFF0F) | 0x0080; // Enable Timer Interrupt
	maskInterrupts(0);

	
	// 0b00010000 for 256x224
	VDP_MODE = 0b00010010;
	
    //Screen display mode
    VDP_DISPMODE = 0x00;

    //Set bitmap render mode to 4bit 512x512
    //0x0 sets bitmap render mode to 8bit 256x256, along with a second 256x bitmap underneath.
    // 0x1 sets bitmap to 8bit 256x512
    VDP_BM_CTRL = 0x0001;

    //Backdrop A/B refers to the "screen buffer"
    //Backdrop refers to solid fill color for each.
    VDP_BACKDROP_A = color(0,0,0);
    VDP_BACKDROP_B = color(0,0,31);
    
    //Color PRIO enables/disables the A/B screens
    VDP_COLORPRIO = 0b01000000;

    // Which bg/obj/bm layers to show
    VDP_LAYER_CTRL = 0b1010101001000110;
    VDP_OBJ_CTRL = 0b000000100000000;
    VDP_BG_CTRL = 0b0000000000001111;
	
	//These establish the bitmaps to cover the screen so that I can just blit the to the screen how I want adjust if you're using smaller bitmaps
	//Set X/Y Screen of the bitmap sprite
	VDP_BMn_SCREENX[0] = 0;
	VDP_BMn_SCREENY[0] = 0;
	
	VDP_BMn_SCROLLY[0] = 0;

	//Width/Height of bitmap sprite
	VDP_BMn_WIDTH[0] = 255;
	VDP_BMn_HEIGHT[0] = 511;

	for(unsigned int i = 0; i < 256; i++){
		VDP_PALETTE[i] = gamepal[i];
	}
	
	
	BiosSoundChannels(0);
	BiosSoundVolume(0, 2);
	BiosSoundVolume(1, 2);
	BiosInitSoundTransmission();
}

void PlayMusic(int i)
{
	//BiosPlayBGM((void *)soundState, 0xFF, i, (void **)trackList);
}

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

void *memcpy(void *restrict dest, const void *restrict src, size_t n)
{
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

// Function to fade in from black to the target palette
void fadeInPalette(int colors) {
    // Gradually fade in 'gamepal' over FADE_STEPS
    for (int step = 1; step <= FADE_STEPS; step++) {
        for (unsigned int i = 0; i < PALETTE_SIZE; i++) {
            // Extract RGB components from 'gamepal[i]' (RGB555 format)
            uint8_t redTarget = (gamepal[i] >> 10) & 0x1F;
            uint8_t greenTarget = (gamepal[i] >> 5) & 0x1F;
            uint8_t blueTarget = gamepal[i] & 0x1F;

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
void fadeOutPalette(int colors) {
    // Gradually fade out 'gamepal' to black over FADE_STEPS
    for (int step = 1; step <= FADE_STEPS; step++) {
        for (unsigned int i = 0; i < PALETTE_SIZE; i++) {
            // Extract RGB components from 'gamepal[i]' (RGB555 format)
            uint8_t red = (gamepal[i] >> 10) & 0x1F;
            uint8_t green = (gamepal[i] >> 5) & 0x1F;
            uint8_t blue = gamepal[i] & 0x1F;

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
