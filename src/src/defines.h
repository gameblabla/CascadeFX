#ifndef DEFINES_H
#define DEFINES_H

#define NECPCFX 1 
#define UNIX 2 
#define CASLOOPY 3

// Base screen dimensions for scaling
#define BASE_SCREEN_WIDTH 256
#define BASE_SCREEN_HEIGHT 240

// Screen dimensions
#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

// Fixed-point arithmetic settings
#define FIXED_POINT_SHIFT 8
#define INT_TO_FIXED(x) ((x) << FIXED_POINT_SHIFT)
#define FIXED_TO_INT(x) ((x) >> FIXED_POINT_SHIFT)

#ifdef HARDWARE_DIV
    #define Division(numerator, denominator) ((numerator) / (denominator))
#else
    #define Division(numerator, denominator) (((numerator) * divTab[(denominator) + DIV_TAB_HALF]) >> DIV_TAB_SHIFT)
#endif

#define SCREEN_WIDTH_HALF (SCREEN_WIDTH / 2)
#define SCREEN_HEIGHT_HALF (SCREEN_HEIGHT / 2)

// Scaling factors as fixed-point values
#define SCALE_FACTOR_X (INT_TO_FIXED(SCREEN_WIDTH) / BASE_SCREEN_WIDTH)
#define SCALE_FACTOR_Y (INT_TO_FIXED(SCREEN_HEIGHT) / BASE_SCREEN_HEIGHT)

// Game settings
#define GRID_WIDTH 12
#define GRID_HEIGHT 12
#define BLOCK_SIZE (FIXED_TO_INT(16 * SCALE_FACTOR_Y))

// 3D settings
#define BASE_CUBE_SIZE 16
#define CUBE_SIZE (FIXED_TO_INT(BASE_CUBE_SIZE * SCALE_FACTOR_Y))
#define DISTANCE_CUBE (CUBE_SIZE / 2)
#define DISTANCE_CUBE_TITLESCREEN 32

// Adjusted projection distance
#define BASE_PROJECTION_DISTANCE -128
#define PROJECTION_DISTANCE (FIXED_TO_INT(BASE_PROJECTION_DISTANCE * SCALE_FACTOR_Y))

#define STARTING_Z_OFFSET -256

// Angle settings
#define ANGLE_MAX 256
#define ANGLE_MASK (ANGLE_MAX - 1)

// Define a global face list for painter's algorithm
#define MAX_FACES 810

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

#define BSWAP16(x) ((((x) & 0xFF00) >> 8) | (((x) & 0x00FF) << 8))

// Casio Loopy

//Controller Buttons
#define P1_A 0x0100
#define P1_B 0x0800
#define P1_C 0x0400
#define P1_D 0x0200
#define P1_UP 0x0001
#define P1_DOWN 0x0002
#define P1_LEFT 0x0004
#define P1_RIGHT 0x0008
#define P1_DET 0x0001
#define P1_START 0x0002
#define P1_TL 0x0004
#define P1_TR 0x0008

// Place critical functions and data in ORAM

#if PLATFORM == CASLOOPY
#define ORAM_DATA __attribute__((section(".oram_data"), used, aligned(4)))
#else
#define ORAM_DATA
#endif


#endif