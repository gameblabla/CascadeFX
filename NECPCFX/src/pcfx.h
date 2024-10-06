#ifndef PCFX_H
#define PCFX_H

#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <eris/types.h>
#include <eris/std.h>
#include <eris/v810.h>
#include <eris/king.h>
#include <eris/tetsu.h>
#include <eris/romfont.h>
#include <eris/timer.h>
#include <eris/cd.h>
#include <eris/pad.h>
#include <eris/7up.h>
#include <eris/low/7up.h>
#include <eris/low/pad.h>
#include <eris/low/scsi.h>
#include <eris/low/soundbox.h>


extern u16 microprog[16];

extern void print_at(int x, int y, int pal, char* str);

extern void eris_king_set_bg_affine_transform(int16_t centerX, int16_t centerY, float zoomX, float zoomY, int rotationAngle);

extern void Set_Video(int bpp);

extern void Initialize_ADPCM(int freq);

extern void Play_ADPCM(int channel, int start_adress, int sizet, unsigned char loop, int freq );

extern void chartou32(char* str, u32* o);

extern void printstr(u32* str, int x, int y, int tall);

extern void Clear_BG0(int bpp);

extern void Upload_Palette(unsigned short pal[], int sizep);

extern void LoadADPCMCD(u32 lba, u32 addr, int size_sample);

extern void Load_PSGSample(u32 lba, int numb, int size_sample);

extern void Play_PSGSample(int ch, int sample_numb, int loop);

extern void initTimer(int psg, int period);

extern __attribute__ ((interrupt)) void my_timer_irq (void);

extern __attribute__ ((interrupt)) void samplepsg_timer_irq (void);

extern void Init_Sprite(int chip);

extern void Update_Sprite(int chip, unsigned short array[], int size_sprite, int offset_spr);

extern void Reset_ADPCM();

extern volatile int __attribute__ ((zda)) frame_text ;

extern void Move_Sprite(int chip, int x, int y);

extern int getFps();

extern int getTicks();

extern void Clear_VDC(int chip);

#define VDC_CHIP_0 0
#define VDC_CHIP_1 1

#define SAMPLES_PSG_NUMBER 1

extern int currentvid;

extern void cd_pausectrl(u8 resume);
extern void initTimer();
extern void Timer_Sample();
extern int GetSeconds();
extern void Reset_ZDA();

#endif