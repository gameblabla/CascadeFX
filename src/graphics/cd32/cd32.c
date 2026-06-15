/**
  @file cd32.c

  This is a small library for the Amiga CD32 that interfaces with AmigaOS 3.1.

  by gameblabla 2023

  Released under CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/)
  plus a waiver of all other intellectual property. The goal of this work is to
  be and remain completely in the public domain forever, available for any use
  whatsoever.
*/

#include "cd32.h"

#define DEPTH 8
#define RBMDEPTH 8
#define ID_BNG   4
#define ID_BNG2   5
#define ID_BORDER 0

#define RBMWIDTH vwidth
#define RBMHEIGHT vheight

uint16_t vwidth;
uint16_t vheight;

uint16_t internal_width;
uint16_t internal_height;

// Global, used for holding buffer.
uint8_t *gfxbuf = NULL;

uint8_t is_pal;

LONG amiga_clock = 3546895; // For PAL. For NTSC : 3579545

struct IntuitionBase	*IntuitionBase = NULL;
struct GfxBase			*GfxBase = NULL;
struct Library			*LowLevelBase = NULL;

struct ViewPort *vport;
struct RastPort *rport;
struct Window *window = NULL;
struct Screen *screen = NULL;
#ifdef SINGLE_BUFFER
struct BitMap *myBitMaps;
#else
struct BitMap **myBitMaps;
struct ScreenBuffer *sb[2];
#endif
struct Library *IconBase = NULL;

void CDDA_Loop_check();

VOID freePlanes(struct BitMap *bitMap, LONG depth, LONG width, LONG height)
{
	SHORT plane_num;
	for (plane_num = 0; plane_num < depth; plane_num++)
	{
		if (NULL != bitMap->Planes[plane_num])
			FreeRaster(bitMap->Planes[plane_num], width, height);
	}
}

LONG setupPlanes(struct BitMap *bitMap, LONG depth, LONG width, LONG height)
{
	SHORT plane_num;
	for (plane_num = 0; plane_num < depth; plane_num++)
	{
		if (NULL != (bitMap->Planes[plane_num]=(PLANEPTR)AllocRaster(width,height)))
			BltClear(bitMap->Planes[plane_num],(width/8)*height,1);
		else
		{
			freePlanes(bitMap, depth, width, height);
			return(0);
		}
	}
	return(TRUE);
}

#ifndef SINGLE_BUFFER
struct BitMap **setupBitMaps(LONG depth, LONG width, LONG height)
{
	static struct BitMap *myBitMaps[3];
	if (NULL != (myBitMaps[0] = (struct BitMap *) AllocMem((LONG)sizeof(struct BitMap), MEMF_CHIP|MEMF_CLEAR)))
	{
        		if (NULL != (myBitMaps[1]=(struct BitMap*)AllocMem((LONG)sizeof(struct BitMap), MEMF_CHIP|MEMF_CLEAR)))
		{
		if (NULL != (myBitMaps[2]=(struct BitMap*)AllocMem((LONG)sizeof(struct BitMap), MEMF_CHIP|MEMF_CLEAR)))
		{
			InitBitMap(myBitMaps[0], depth, width, height);
			InitBitMap(myBitMaps[1], depth, width, height);
           	InitBitMap(myBitMaps[2], depth, width, height);
			if (0 != setupPlanes(myBitMaps[0], depth, width, height))
			{
				if (0 != setupPlanes(myBitMaps[1], depth, width, height))
                {
                				if (0 != setupPlanes(myBitMaps[2], depth, width, height))
					return(myBitMaps);
                    				   freePlanes(myBitMaps[1], depth, width, height);
			    }
        		 				    freePlanes(myBitMaps[0], depth, width, height);
                    }
		  			  FreeMem(myBitMaps[2], (LONG)sizeof(struct BitMap));
		}
	   			   FreeMem(myBitMaps[1], (LONG)sizeof(struct BitMap));
	}
		FreeMem(myBitMaps[0], (LONG)sizeof(struct BitMap));
	}
	return(NULL);
}
#endif

#ifdef SINGLE_BUFFER
VOID	freeBitMaps(struct BitMap *myBitMaps,
#else
VOID	freeBitMaps(struct BitMap **myBitMaps,
#endif
					LONG depth, LONG width, LONG height)
{
#ifdef SINGLE_BUFFER
	freePlanes(myBitMaps, depth, width, height);
	FreeMem(myBitMaps, (LONG)sizeof(struct BitMap));
#else
	freePlanes(myBitMaps[0], depth, width, height);
	freePlanes(myBitMaps[1], depth, width, height);
	freePlanes(myBitMaps[2], depth, width, height);
	FreeMem(myBitMaps[0], (LONG)sizeof(struct BitMap));
	FreeMem(myBitMaps[1], (LONG)sizeof(struct BitMap));
	FreeMem(myBitMaps[2], (LONG)sizeof(struct BitMap));
#endif
}



void FreeTempRP( struct RastPort *rp )
{
	if( rp )
	{
		if( rp->BitMap )
		{
			//if( rev3 )
         //    FreeBitMap( rp->BitMap );
			//else myFreeBitMap( rp->BitMap );
		}
		FreeVec( rp );
	}
}

struct RastPort *MakeTempRP( struct RastPort *org )
{
	struct RastPort *rp;

	if( rp = AllocVec(sizeof(*rp), MEMF_ANY) )
	{
		memcpy( rp, org, sizeof(*rp) );
		rp->Layer = NULL;

	    rp->BitMap=AllocBitMap(org->BitMap->BytesPerRow*8,1,org->BitMap->Depth,0,org->BitMap);
		if( !rp->BitMap )
		{
			FreeVec( rp );
			rp = NULL;
		}
	}

	return rp;
}



static UWORD __chip emptypointer[] = 
{
	0x0000, 0x0000,	/* reserved, must be NULL */
	0x0000, 0x0000, 	/* 1 row of image data */
	0x0000, 0x0000	/* reserved, must be NULL */
};
static struct ScreenModeRequester *video_smr = NULL;
int mode = 0;
ULONG propertymask;
struct RastPort temprp2;
UBYTE toggle=0;

#ifndef SINGLE_BUFFER
inline void Update_Video()
{	
	WaitTOF();
	while (!ChangeScreenBuffer(screen,sb[toggle]))
	toggle^=1;
	temprp2.BitMap=myBitMaps[toggle];
}
#endif

void SetPalette_Video(const uint8_t *palette, uint16_t numberofcolors)
{
#if 1
    uint_fast16_t i;
    ULONG table[256*3+1+1];
    table[0]=(numberofcolors<<16)|0;
	for (i = 0; i < numberofcolors; i++)
    {
		// << 26 for VGA palette, << 24 for normal ones
        table[i*3+1]= ((ULONG)palette[i*3+0] << 24);
        table[i*3+2]= ((ULONG)palette[i*3+1] << 24);
        table[i*3+3]= ((ULONG)palette[i*3+2] << 24);
    }    
    table[1+numberofcolors*3] = 0;  
    LoadRGB32(vport,table);
#endif
/*
#if 0
		volatile struct Custom* custom = (volatile struct Custom*) 0xDFF000;
		UWORD colorIndex;
		uint8_t palette[256 * 3];
		
		memcpy(palette, pal, 256 * 3);
		
		volatile uint16_t* const DMACONW     = (uint16_t* const) 0xDFF096;
		volatile uint16_t* const INTENAW     = (uint16_t* const) 0xDFF09A;
		volatile uint16_t* const INTREQW     = (uint16_t* const) 0xDFF09C;

        //*INTREQW = 0x7FFF; // Idem. Original Scoopex code do this twice. I have removed without problems.
        *DMACONW = 0x7FFF; // Disable all bits in DMACON
        *DMACONW = 0x87E0; // Setting DMA channels
		
		*(volatile uint16_t *)0xDFF000 = 0xfff;
		
		// Save the existing bplcon3 settings
		ULONG origBplcon3 = custom->bplcon3;

		// Loop through all palettes
		for (UWORD paletteIndex = 0; paletteIndex < 8; paletteIndex++) {
			// Select the current palette
			custom->bplcon3 = (origBplcon3 & ~(0b111 << 13)) | (paletteIndex << 13);

			// Clear LOCT bit
			custom->bplcon3 = origBplcon3 & ~(1 << 9);

			// Set the high nibbles of each color component
			for (colorIndex = 0; colorIndex < 32; colorIndex++) {
				custom->color[colorIndex] = ((palette[colorIndex * 3 + 0] & 0xF0) << 4) |
											((palette[colorIndex * 3 + 1] & 0xF0) << 0) |
											((palette[colorIndex * 3 + 2] & 0xF0) >> 4);
			}

			// Set LOCT bit
			custom->bplcon3 = origBplcon3 | (1 << 9);

			// Set the low nibbles of each color component
			for (colorIndex = 0; colorIndex < 32; colorIndex++) {
				custom->color[colorIndex] = ((palette[colorIndex * 3 + 0] & 0x0F) << 8) |
											((palette[colorIndex * 3 + 1] & 0x0F) << 4) |
											((palette[colorIndex * 3 + 2] & 0x0F) << 0);
			}
		}

		// Restore the original bplcon3 settings
		custom->bplcon3 = origBplcon3;
    
#endif
*/
}

//struct ExecBase* SysBase = NULL;

int Init_Video(uint16_t w, uint16_t h, uint16_t iwidth, uint16_t iheight, uint8_t akikouse)
{
	if (!SysBase) SysBase = (struct ExecBase*) OpenLibrary("exec.library", 0);
	
	/* I would love so much not having to depend on NDK and use direct hardware access... */
 	if (!LowLevelBase) LowLevelBase 	= OpenLibrary("lowlevel.library",39);

	if (!IntuitionBase)
	{
		if ((IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library",0)) == NULL)
		{
			LOG("No intution library\n");
			return 1;
		}
	}
	
	/* Open graphics.library */
	if (!GfxBase)
	{
		if ((GfxBase = (struct GfxBase *) OpenLibrary("graphics.library", 0)) == NULL)
		{
			LOG("No graphics library\n");
			return 1;
		}
	}
	
	/* IF akiko was used before, free the memory before initializing it again */
	if (gfxbuf)
	{
		FreeMem(gfxbuf, internal_width * internal_height);
		gfxbuf = NULL;
	}
	
	vwidth = w;
	vheight = h;
	internal_width = iwidth;
	internal_height = iheight;
	
	mode = BestModeID (BIDTAG_NominalWidth, vwidth,
	BIDTAG_NominalHeight, vheight,
	BIDTAG_Depth, DEPTH,
	BIDTAG_DIPFMustNotHave, propertymask,
	TAG_DONE);
		
		
#ifdef SINGLE_BUFFER
	if (!myBitMaps)
	{
		myBitMaps = AllocMem((LONG)sizeof(struct BitMap), MEMF_CHIP|MEMF_CLEAR);
		InitBitMap(myBitMaps, RBMDEPTH, RBMWIDTH, RBMHEIGHT);
		setupPlanes(myBitMaps, RBMDEPTH, RBMWIDTH, RBMHEIGHT);
	}
#endif
	
#ifndef SINGLE_BUFFER
	if (NULL!=(myBitMaps=setupBitMaps(RBMDEPTH,RBMWIDTH,RBMHEIGHT)))
#endif
	{
		if (screen = OpenScreenTags( NULL,
		SA_Width, vwidth,
		SA_Height, vheight,
		SA_Depth, DEPTH,
		SA_DisplayID, mode,
		SA_Quiet, TRUE,
		SA_Type, CUSTOMSCREEN|CUSTOMBITMAP,
#ifdef SINGLE_BUFFER
		SA_BitMap,myBitMaps,
#else
		SA_BitMap,myBitMaps[0],
#endif
		TAG_DONE))
		{
			vport = &screen->ViewPort;
#ifndef SINGLE_BUFFER
			sb[0] = AllocScreenBuffer(screen, myBitMaps[0],0);
			sb[1] = AllocScreenBuffer(screen, myBitMaps[1],0);
#endif
			if (window = OpenWindowTags( NULL,
			WA_CloseGadget, FALSE,
			WA_DepthGadget, FALSE,
			WA_DragBar, FALSE,
			WA_SizeGadget, FALSE,
			WA_Gadgets, FALSE,
			WA_Borderless, TRUE,
			WA_NoCareRefresh, TRUE,
			WA_SimpleRefresh, TRUE,
			WA_RMBTrap, FALSE,
			WA_Activate, TRUE,
			WA_IDCMP, NULL,
			WA_Width, vwidth,/*WIDTH,   */
			WA_Height, vheight,/* HEIGHT,    */
			WA_CustomScreen, screen,
			TAG_DONE) )
			{
				rport = window->RPort;
			}
		}
	}

	if (!gfxbuf)
	{
		if (akikouse == 1)
		{
			gfxbuf = AllocMem( sizeof(uint8_t)*(internal_width*internal_height), MEMF_PUBLIC|MEMF_CLEAR);
		}
	}
	
	InitRastPort(&temprp2);
	temprp2.Layer=0;
#ifdef SINGLE_BUFFER
	temprp2.BitMap=myBitMaps;
#else
	temprp2.BitMap=myBitMaps[toggle^1];
#endif
	SetPointer(window,emptypointer,1,16,0,0);
	
	// Check if we're NTSC or PAL
	is_pal = (((struct GfxBase *) GfxBase)->DisplayFlags & PAL) == PAL;
    if (is_pal) {
        amiga_clock = 3546895; // We are in PAL mode
    } else {
		amiga_clock = 3579545;
    }
	
	return 0;
}

/* Drawing stuff */

ULONG LoadFile_tobuffer(const char* fname, uint8_t* buffer)
{
    BPTR file;
    ULONG size;
	file = Open(fname, MODE_OLDFILE);
    Seek(file, 0, OFFSET_END);
    size = Seek(file, 0, OFFSET_BEGINNING);
    Read(file, buffer, size);
    Close(file);
    
    return size;
}

void LoadPalette_fromfile_LoadRGB(const char* fname)
{
    BPTR file;
	char* palette;
    ULONG size;
	file = Open(fname, MODE_OLDFILE);
    Seek(file, 0, OFFSET_END);
    size = Seek(file, 0, OFFSET_BEGINNING);
    palette = AllocMem(size, MEMF_CHIP|MEMF_CLEAR);
    Read(file, palette, size);
    Close(file);
    LoadRGB32(vport, (UWORD*)palette);
    
    FreeMem(palette, size);
}

void LoadPalette_fromfile_RAW(const char* fname)
{
	uint_fast16_t i;
	uint16_t numberofcolors;
	ULONG table[256*3+1+1];
    BPTR file;
	unsigned char cur_pal[768];
    ULONG size;
 
	file = Open(fname, MODE_OLDFILE);
    Seek(file, 0, OFFSET_END);
    size = Seek(file, 0, OFFSET_BEGINNING);
    Read(file, cur_pal, size);
    Close(file);
    
    numberofcolors = size / 3;
    
    table[0]=(numberofcolors<<16)|0;
	for (i = 0; i < numberofcolors; i++)
    {
		// << 26 for VGA palette, << 24 for normal ones
        table[i*3+1]= ((ULONG)cur_pal[i*3+0] << 24);
        table[i*3+2]= ((ULONG)cur_pal[i*3+1] << 24);
        table[i*3+3]= ((ULONG)cur_pal[i*3+2] << 24);
    }    
    table[1+numberofcolors*3] = 0;  
    LoadRGB32(vport,table);
}

ULONG LoadImage_native(const char* fname, struct ImageVSprite* buffer, uint16_t width, uint16_t height)
{
    ULONG size;

    buffer->x = 0;
    buffer->y = 0;
    buffer->width = width;
    buffer->height = height;
    
	BPTR file = Open(fname, MODE_OLDFILE);
    Seek(file, 0, OFFSET_END);
    size = Seek(file, 0, OFFSET_BEGINNING);
	// 3.1 are actually decently fast and support interleaved frames
    buffer->imgdata = AllocBitMap(width, height, DEPTH, BMF_INTERLEAVED, NULL);
    Read(file, buffer->imgdata->Planes[0], size);
	Close(file);
    
    return size;
}
