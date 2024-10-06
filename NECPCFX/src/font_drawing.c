#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "font_drawing.h"
#include "font_menudata.h"

#define HOST_WIDTH_RESOLUTION 256
#define HOST_HEIGHT_RESOLUTION 240

#define BSWAP16(x) ((((x) & 0xFF00) >> 8) | (((x) & 0x00FF) << 8))


#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240
extern uint16_t framebuffer_game[SCREEN_WIDTH*SCREEN_HEIGHT/2];

static inline void SetPixel16(int32_t x, int32_t y, int32_t color) {
    int32_t index = y * SCREEN_WIDTH + x;
    ((uint16_t*)framebuffer_game)[index >> 1] = color;
}


// Function to read a 16-bit word from the framebuffer, handling endianess
static inline uint16_t GetPixel16(int32_t x, int32_t y) {
    // Calculate the address in the framebuffer
    uint16_t *fb = (uint16_t *)framebuffer_game;
    uint16_t data = fb[y * (SCREEN_WIDTH / 2) + (x / 2)];

    // Byteswap to convert from big endian framebuffer to little endian CPU
    return BSWAP16(data);
}

// Modified SetPixel8 function
static inline void setPixel(int32_t x, int32_t y, uint8_t color) {
    // Read the existing 16-bit word from the framebuffer
    uint16_t existing_data = GetPixel16(x & ~1, y); // Align x to even position

    // Modify the appropriate byte
    if (x & 1) {
        // x is odd, modify lower byte
        existing_data = (existing_data & 0xFF00) | color;
    } else {
        // x is even, modify upper byte
        existing_data = (existing_data & 0x00FF) | (color << 8);
    }

    // Write back the modified 16-bit word using SetPixel16
    SetPixel16(x & ~1, y, existing_data);
}


static int32_t isOutlinePixel(uint8_t* charfont, int32_t x, int32_t y)
{
	int32_t xis0 = !x, xis7 = x == 7, yis0 = !y, yis7 = y == 7;
	
	if(xis0)
	{
		if(yis0)
		{
			return !(*charfont & 0x80) && ((*charfont & 0x40) || (charfont[1] & 0x80) || (charfont[1] & 0x40));
		}
		else if(yis7)
		{
			return !(charfont[7] & 0x80) && ((charfont[7] & 0x40) || (charfont[6] & 0x80) || (charfont[6] & 0x40));
		}
		else
		{
			return !(charfont[y] & 0x80) && (
				(charfont[y - 1] & 0x80) || (charfont[y - 1] & 0x40) ||
				(charfont[y] & 0x40) ||
				(charfont[y + 1] & 0x80) || (charfont[y + 1] & 0x40));
		}
	}
	else if(xis7)
	{
		if(yis0)
		{
			return !(*charfont & 0x01) && ((*charfont & 0x02) || (charfont[1] & 0x01) || (charfont[1] & 0x02));
		}
		else if(yis7)
		{
			return !(charfont[7] & 0x01) && ((charfont[7] & 0x02) || (charfont[6] & 0x01) || (charfont[6] & 0x02));
		}
		else
		{
			return !(charfont[y] & 0x01) && (
				(charfont[y - 1] & 0x01) || (charfont[y - 1] & 0x02) ||
				(charfont[y] & 0x02) ||
				(charfont[y + 1] & 0x01) || (charfont[y + 1] & 0x02));
		}
	}
	else
	{
		int32_t b = 1 << (7 - x);
		if(yis0)
		{
			return !(*charfont & b) && (
				(*charfont & (b << 1)) || (*charfont & (b >> 1)) ||
				(charfont[1] & (b << 1)) || (charfont[1] & b) || (charfont[1] & (b >> 1)));
		}
		else if(yis7)
		{
			return !(charfont[7] & b) && (
				(charfont[7] & (b << 1)) || (charfont[7] & (b >> 1)) ||
				(charfont[6] & (b << 1)) || (charfont[6] & b) || (charfont[6] & (b >> 1)));
		}
		else
		{
			return !(charfont[y] & b) && (
				(charfont[y] & (b << 1)) || (charfont[y] & (b >> 1)) ||
				(charfont[y - 1] & (b << 1)) || (charfont[y - 1] & b) || (charfont[y - 1] & (b >> 1)) ||
				(charfont[y + 1] & (b << 1)) || (charfont[y + 1] & b) || (charfont[y + 1] & (b >> 1)));
		}
	}
}

static void drawChar(uint8_t* restrict buffer, int32_t *x, int32_t *y, int32_t margin, char ch, uint16_t fc, uint16_t olc)
{
	int32_t i, j;
	uint8_t *charSprite;
	if (ch == '\n')
	{
		*x = margin;
		*y += 8;
	}
	else if(*y < HOST_HEIGHT_RESOLUTION-1)
	{
		charSprite = ch * 8 + n2DLib_font;
		// Draw charSprite as monochrome 8*8 image using given color
		for(i = 0; i < 8; i++)
		{
			for(j = 7; j >= 0; j--)
			{
				if((charSprite[i] >> j) & 1)
				{
					setPixel(*x + (7 - j), *y + i, fc);
				}
				else if(isOutlinePixel(charSprite, 7 - j, i))
				{
					setPixel( *x + (7 - j), *y + i, olc);
				}
			}
		}
		*x += 8;
	}
}

static void drawString(uint8_t* buffer, int32_t *x, int32_t *y, int32_t _x, const char *str, uint16_t fc, uint16_t olc)
{
	unsigned long i, max = strlen(str) + 1;
	for(i = 0; i < max; i++)
		drawChar(buffer, x, y, _x, str[i], fc, olc);
}

void print_string(const char *s,const uint16_t fg_color, const uint16_t bg_color, int32_t x, int32_t y, uint8_t* buffer) 
{
	drawString(buffer, &x, &y, 0, s, fg_color, bg_color);
}
