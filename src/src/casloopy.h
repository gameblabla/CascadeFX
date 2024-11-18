#ifndef _CASLOOPY_
#define _CASLOOPY_

#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>


#define PALETTE_SIZE 256 // Assuming a palette of 256 colors
#define FADE_STEPS 31 // Define the number of steps for the fade effect

extern void PlayMusic(int i);
extern void Init_Video_Game();

extern volatile void (*BiosVsync)(void);
extern void Empty_Palette();

extern void memcpy32(void *dest, const void *src, size_t n);

static inline void CopyFrameBuffer(int* source, int* destination)
{
    int* const end = destination + (256*240)/2;
    while(destination < end)
    {
        destination[0] = source[0];
        destination[1] = source[1];
        destination[2] = source[2];
        destination[3] = source[3];
        destination[4] = source[4];
        destination[5] = source[5];
        destination[6] = source[6];
        destination[7] = source[7];
        destination[8] = source[8];
        destination[9] = source[9];
        destination[10] = source[10];
        destination[11] = source[11];
        destination[12] = source[12];
        destination[13] = source[13];
        destination[14] = source[14];
        destination[15] = source[15];
        destination += 16;
        source += 16;
    }
}

// Function to fade in from black to pict6pal
extern void fadeInPalette( int colors);
// Function to fade out from pict6pal to black
extern void fadeOutPalette(int colors);

#endif