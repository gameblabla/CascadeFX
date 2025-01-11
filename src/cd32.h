/**
  @file cd32.h

  This is a small library for the Amiga CD32 that interfaces with AmigaOS 3.1.

  by gameblabla 2023

  Released under CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/)
  plus a waiver of all other intellectual property. The goal of this work is to
  be and remain completely in the public domain forever, available for any use
  whatsoever.
*/

#ifndef CD32_H
#define CD32_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <intuition/intuition.h>
#include <clib/exec_protos.h>
#include <clib/graphics_protos.h>
#include <clib/intuition_protos.h>
#include <clib/lowlevel_protos.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/alib_protos.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <pragmas/intuition_pragmas.h>
#include <pragmas/graphics_pragmas.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <exec/exec.h>
#include <exec/types.h>
#include <devices/audio.h>
#include <devices/cd.h>
#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <libraries/lowlevel.h>

#define SINGLE_BUFFER 1
#define LOOP_CDDA 1
#define NOLOOP_CDDA 0

#ifdef DEBUG
#define LOG printf
#else
// Empty stub for release builds
#define LOG(...) ((void)0)
#endif

extern uint8_t *gfxbuf;
extern uint16_t internal_width;
extern uint16_t internal_height;
extern uint16_t vwidth;
extern uint16_t vheight;
extern struct RastPort temprp2;

struct ImageVSprite
{
	int16_t x;
	int16_t y;
	uint16_t width;
	uint16_t height;
	uint8_t frames;
	struct BitMap *imgdata;
};

#if SINGLE_BUFFER
extern struct Window *window;
#define FINAL_BITMAP window->RPort->BitMap
#else
extern struct BitMap **myBitMaps;
#define FINAL_BITMAP myBitMaps[bufferSwap]
#endif

#define my_malloc(x) AllocMem(x, 0)

/*
 * Creates the display that you are going to draw the graphics to.
 * Needed for game_setpalette and Video_UpdateScreen
*/

extern int Init_Video(uint16_t w, uint16_t h, uint16_t internal_width, uint16_t internal_height, uint8_t akikouse);

/*
 * Creates the display that you are going to draw the graphics to.
 * numberofcolors is the numberofcolors the palette has (It should be 256 by default).
 * If the buffer is 8bpp but the palette only has 64 entries, adjust accordingly.
*/

extern  void SetPalette_Video(const uint8_t *palette, uint16_t numberofcolors);

/*
 * Flips the screen.
 * 
 * Note that for CDDA music looping, this is also required to be called in a looping function.
 * 
*/
#define Draw_Video_Akiko() WriteChunkyPixels(&temprp2,0,0,vwidth-1,((vheight)-1),gfxbuf,vwidth); WaitTOF();

/*
 * Flips only a selected part of the screen.
 * Useful if you only need to update a part of the screen.
 * For example, for a 3D game with a status bar, only update the status bar when it changes. 
*/

#define Draw_Video_Akiko_partial(x1, y1, x2, y2) WriteChunkyPixels(&temprp2,x1,y1,x2,y2,gfxbuf,vwidth); WaitTOF();

#ifdef SINGLE_BUFFER
#define Update_Video() WaitTOF();
#else
extern inline void Update_Video();
#endif

/*
 * Load a palette from file and applies it to the system palette.
 * 256 colors are expected. The palette file should be in the Game/ folder.
*/
extern void LoadPalette_fromfile_RAW(const char* fname);
extern void LoadPalette_fromfile_LoadRGB(const char* fname);

/*
 * Loads a file (The file should be in the Game/ folder) and uploads it to the selected buffer.
 * Use this for say, loading an image file to the screen buffer for instance.
 * That said, it can also be used for other things too. (For example loading a text file)
*/
extern ULONG LoadFile_tobuffer(const char* fname, uint8_t* buffer);

extern ULONG LoadImage_native(const char* fname, struct ImageVSprite* buffer, uint16_t width, uint16_t height);

#define DrawImage_native(a) \
	BltBitMap(a.imgdata, 0, 0, FINAL_BITMAP, a.x, a.y, a.width, a.height, 0xC0, 0xFF, NULL);

/* CDTV/CDXL stuff */

extern void CDPLAYER_PlayVideo(LONG NumPlays, char* fname, uint32_t speed, uint_fast8_t InputStopPlayback);


#endif
