#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "loopy.h"
#include "font_drawing.h"
#include "gamepal.h"
#include "bg.h"
#include "textures.h"
#include "title.h"
#include "trig.h"

/* CAS LOOPY SPECIFIC */

#define PALETTE_SIZE 256 // Assuming a palette of 256 colors
#define FADE_STEPS 31 // Define the number of steps for the fade effect


volatile void (*BiosVsync)(void) = (void (*)(void))0x6A5A;

void EmptyPalette()
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
void fadeInPalette(const uint16_t* topal) {
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
void fadeOutPalette(const uint16_t* frompal) {
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


int16_t framebuffer_game[256*240/2];

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

int32_t oldpad1, oldpad0;
int32_t newpad1, newpad0;

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

char* itoa(int value) {
    static char buffer[12];  // Enough for an integer (-2147483648 has 11 characters + 1 for '\0')
    char* ptr = buffer + sizeof(buffer) - 1;
    int is_negative = 0;

    // Null-terminate the buffer
    *ptr = '\0';

    // Handle negative numbers
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    // Process each digit
    do {
        *--ptr = (value % 10) + '0';
        value /= 10;
    } while (value);

    // Add the negative sign if necessary
    if (is_negative) {
        *--ptr = '-';
    }

    return ptr;
}




/* END */

#if 1

#define SWAP(a, b) do { \
    __typeof__(a) _temp = (a); \
    (a) = (b); \
    (b) = _temp; \
} while (0)

static inline void swap_elements(void *a, void *b, size_t size) {
    unsigned char *pa = (unsigned char*)a;
    unsigned char *pb = (unsigned char*)b;
    
    // Operate on 32-bit chunks if size is a multiple of 4
    while (size >= sizeof(uint32_t)) {
        SWAP(*(uint32_t*)pa, *(uint32_t*)pb);
        pa += sizeof(uint32_t);
        pb += sizeof(uint32_t);
        size -= sizeof(uint32_t);
    }

    // Operate on 16-bit chunks if size is a multiple of 2
    while (size >= sizeof(uint16_t)) {
        SWAP(*(uint16_t*)pa, *(uint16_t*)pb);
        pa += sizeof(uint16_t);
        pb += sizeof(uint16_t);
        size -= sizeof(uint16_t);
    }

    // Operate on the remaining bytes (if any)
    while (size > 0) {
        SWAP(*pa, *pb);
        pa++;
        pb++;
        size--;
    }
}

static inline int partition(void *base, size_t size, int low, int high, int (*compar)(const void *, const void *)) {
    char *arr = (char*)base;
    char *pivot = arr + high * size;
    int i = low - 1;

    for (int j = low; j < high; j++) {
        if (compar(arr + j * size, pivot) <= 0) {
            i++;
            swap_elements(arr + i * size, arr + j * size, size);
        }
    }
    swap_elements(arr + (i + 1) * size, arr + high * size, size);
    return i + 1;
}

static inline void quicksort(void *base, size_t size, int low, int high, int (*compar)(const void *, const void *)) {
    if (low < high) 
    {
        int pi = partition(base, size, low, high, compar);

        quicksort(base, size, low, pi - 1, compar);
        quicksort(base, size, pi + 1, high, compar);
    }
}

static void qsort(void *base, size_t num, size_t size, int (*compar)(const void *, const void *)) {
    quicksort(base, size, 0, num - 1, compar);
}
#endif

#define _16BITS_WRITES
#define BIGENDIAN_TEXTURING 1
//#define FORCE_FULLSCREEN_DRAWS 1

// Base screen dimensions for scaling
#define BASE_SCREEN_WIDTH 256
#define BASE_SCREEN_HEIGHT 240

// Fixed-point arithmetic settings
#define FIXED_POINT_SHIFT 8
#define FIXED_POINT_SCALE (1 << FIXED_POINT_SHIFT)

// Screen dimensions
#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

#define SCREEN_WIDTH_HALF (SCREEN_WIDTH / 2)
#define SCREEN_HEIGHT_HALF (SCREEN_HEIGHT / 2)

// Scaling factors as fixed-point values
#define SCALE_FACTOR_X ((SCREEN_WIDTH << FIXED_POINT_SHIFT) / BASE_SCREEN_WIDTH)
#define SCALE_FACTOR_Y ((SCREEN_HEIGHT << FIXED_POINT_SHIFT) / BASE_SCREEN_HEIGHT)

// Game settings
#define GRID_WIDTH 12
#define GRID_HEIGHT 12
#define BLOCK_SIZE ((16 * SCALE_FACTOR_Y) >> FIXED_POINT_SHIFT)

// 3D settings
#define BASE_CUBE_SIZE 16
#define CUBE_SIZE ((BASE_CUBE_SIZE * SCALE_FACTOR_Y) >> FIXED_POINT_SHIFT)
#define DISTANCE_CUBE (CUBE_SIZE / 2)
#define DISTANCE_CUBE_TITLESCREEN 32

// Adjusted projection distance
#define BASE_PROJECTION_DISTANCE -128
#define PROJECTION_DISTANCE ((BASE_PROJECTION_DISTANCE * SCALE_FACTOR_Y) >> FIXED_POINT_SHIFT)

#define STARTING_Z_OFFSET -256

// Angle settings
#define ANGLE_MAX 256
#define ANGLE_MASK (ANGLE_MAX - 1)

// Define a global face list for painter's algorithm
#define MAX_FACES 900

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

int force_redraw = 0;
int force_redraw_puzzle = 0;
bool backtitle = false;

// Structure to represent a 3D point
typedef struct {
    int32_t x, y, z;
} Point3D;

// Structure to represent a 2D point with texture coordinates
typedef struct {
    int32_t x, y;
    uint32_t u, v;
} Point2D;

// Structure to represent a face
typedef struct {
    int vertex_indices[4];
} Face;


// Game state variables
uint8_t grid[GRID_HEIGHT * GRID_WIDTH];
int score = 0;
bool game_over = false;

uint32_t drop_interval = 30; // Drop every 500ms

// Background state variables
int clear_background_frames = 0;
bool almost_losing = false;

// Current piece
typedef struct {
    int type;
    int rotation;
    int x, y;
} Piece;

Piece current_piece;

void load_puzzle(int index);
void Game_Switch(int state);

// Define macro for puzzle piece indexing
#define PUZZLE_PIECE_INDEX(type, rotation, i, j) \
    ((((type) * 4 + (rotation)) * 4 + (i)) * 4 + (j))

// Puzzle pieces (shapes) as a 1D array
const uint8_t puzzle_pieces[7 * 4 * 4 * 4] = {
    // Flattened data
    // Type 0 (I)
    // Rotation 0
    0,0,0,0,  1,1,1,1,  0,0,0,0,  0,0,0,0,
    // Rotation 1
    0,0,1,0,  0,0,1,0,  0,0,1,0,  0,0,1,0,
    // Rotation 2
    0,0,0,0,  0,0,0,0,  1,1,1,1,  0,0,0,0,
    // Rotation 3
    0,1,0,0,  0,1,0,0,  0,1,0,0,  0,1,0,0,
    // Type 1 (O)
    // Rotation 0
    0,0,0,0,  0,1,1,0,  0,1,1,0,  0,0,0,0,
    // Rotation 1
    0,0,0,0,  0,1,1,0,  0,1,1,0,  0,0,0,0,
    // Rotation 2
    0,0,0,0,  0,1,1,0,  0,1,1,0,  0,0,0,0,
    // Rotation 3
    0,0,0,0,  0,1,1,0,  0,1,1,0,  0,0,0,0,
    // Type 2 (T)
    // Rotation 0
    0,0,0,0,  1,1,1,0,  0,1,0,0,  0,0,0,0,
    // Rotation 1
    0,0,1,0,  0,1,1,0,  0,0,1,0,  0,0,0,0,
    // Rotation 2
    0,0,0,0,  0,1,0,0,  1,1,1,0,  0,0,0,0,
    // Rotation 3
    0,1,0,0,  0,1,1,0,  0,1,0,0,  0,0,0,0,
    // Type 3 (S)
    // Rotation 0
    0,0,0,0,  0,1,1,0,  1,1,0,0,  0,0,0,0,
    // Rotation 1
    0,1,0,0,  0,1,1,0,  0,0,1,0,  0,0,0,0,
    // Rotation 2
    0,0,0,0,  0,1,1,0,  1,1,0,0,  0,0,0,0,
    // Rotation 3
    0,1,0,0,  0,1,1,0,  0,0,1,0,  0,0,0,0,
    // Type 4 (Z)
    // Rotation 0
    0,0,0,0,  1,1,0,0,  0,1,1,0,  0,0,0,0,
    // Rotation 1
    0,0,1,0,  0,1,1,0,  0,1,0,0,  0,0,0,0,
    // Rotation 2
    0,0,0,0,  1,1,0,0,  0,1,1,0,  0,0,0,0,
    // Rotation 3
    0,0,1,0,  0,1,1,0,  0,1,0,0,  0,0,0,0,
    // Type 5 (J)
    // Rotation 0
    0,0,0,0,  1,1,1,0,  0,0,1,0,  0,0,0,0,
    // Rotation 1
    0,0,1,0,  0,0,1,0,  0,1,1,0,  0,0,0,0,
    // Rotation 2
    0,0,0,0,  1,0,0,0,  1,1,1,0,  0,0,0,0,
    // Rotation 3
    0,1,1,0,  0,1,0,0,  0,1,0,0,  0,0,0,0,
    // Type 6 (L)
    // Rotation 0
    0,0,0,0,  1,1,1,0,  1,0,0,0,  0,0,0,0,
    // Rotation 1
    0,1,0,0,  0,1,0,0,  0,1,1,0,  0,0,0,0,
    // Rotation 2
    0,0,0,0,  0,0,1,0,  1,1,1,0,  0,0,0,0,
    // Rotation 3
    0,1,1,0,  0,0,1,0,  0,0,1,0,  0,0,0,0
};

// Define macro for cube texture coordinates indexing
#define CUBE_TEXCOORDS_TEMPLATE_INDEX(i, j) ((i) * 2 + (j))

// Common cube data
static const Point3D cube_vertices_template[8] = {
    {-DISTANCE_CUBE,  DISTANCE_CUBE,  DISTANCE_CUBE}, // 0
    { DISTANCE_CUBE,  DISTANCE_CUBE,  DISTANCE_CUBE}, // 1
    { DISTANCE_CUBE, -DISTANCE_CUBE,  DISTANCE_CUBE}, // 2
    {-DISTANCE_CUBE, -DISTANCE_CUBE,  DISTANCE_CUBE}, // 3
    {-DISTANCE_CUBE,  DISTANCE_CUBE, -DISTANCE_CUBE}, // 4
    { DISTANCE_CUBE,  DISTANCE_CUBE, -DISTANCE_CUBE}, // 5
    { DISTANCE_CUBE, -DISTANCE_CUBE, -DISTANCE_CUBE}, // 6
    {-DISTANCE_CUBE, -DISTANCE_CUBE, -DISTANCE_CUBE}  // 7
};

static const uint32_t cube_texcoords_template[8 * 2] = {
    0<<8, 0<<8,    // Vertex 0
    31<<8, 0<<8,   // Vertex 1
    31<<8, 31<<8,  // Vertex 2
    0<<8, 31<<8,   // Vertex 3
    0<<8, 0<<8,    // Vertex 4
    31<<8, 0<<8,   // Vertex 5
    31<<8, 31<<8,  // Vertex 6
    0<<8, 31<<8    // Vertex 7
};

// Cube faces template
static const Face cube_faces_template[6] = {
    {{0, 1, 2, 3}},   // Front face
    {{5, 4, 7, 6}},   // Back face
    {{4, 0, 3, 7}},   // Left face
    {{1, 5, 6, 2}},   // Right face
    {{4, 5, 1, 0}},   // Top face
    {{3, 2, 6, 7}}    // Bottom face
};

// Function to set a pixel using 8-bit palette index
static inline void setPixel(uint32_t x, uint32_t y, uint8_t color) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    ((uint8_t*)framebuffer_game)[y * SCREEN_WIDTH + x] = color;
}

// Pixel setting functions
static inline void SetPixel8(int32_t x, int32_t y, int32_t color) {
    ((uint8_t*)framebuffer_game)[y * SCREEN_WIDTH + x] = color;
}

static inline void SetPixel16(int32_t x, int32_t y, int32_t color) {
    int32_t index = y * SCREEN_WIDTH + x;
    ((uint16_t*)framebuffer_game)[index >> 1] = color;
}

// Helper function to fetch the texture color
static inline uint32_t fetchTextureColor(int32_t u, int32_t v, int tetromino_type) {
    uint8_t tex_u = ((u >> 8) & 31);
    uint8_t tex_v_full = ((v >> 8) & 31);
    bool is_brighter = tex_v_full >= 16;
    uint8_t local_v = tex_v_full & 0x0F;
    int tile_index = tetromino_type * 2 + (is_brighter ? 1 : 0);
    if (tile_index >= 14) tile_index = 0;
    
    // Calculate the index in the texture array
    int index = tile_index * 32 * 16 + local_v * 32 + tex_u;
    
    // Since texture is stored as uint16_t but contains 8-bit values, we access it as uint16_t*
    uint8_t *texture16 = (uint8_t *)texture;
    
    // Extract the 8-bit value from the 16-bit storage
    uint8_t color = (uint8_t)(texture16[index] ); // Assuming lower byte contains the color index
    
    return color;
}

static inline void drawScanline(int32_t xs, int32_t xe, int32_t u, int32_t v,
    int32_t du, int32_t dv, int y, int tetromino_type) {
#if defined(_8BITS_WRITES)
    for (int32_t x = xs; x <= xe; x++) {
        int32_t color = fetchTextureColor(u, v, tetromino_type);
        setPixel(x, y, color);
        u += du;
        v += dv;
    }
#elif defined(_16BITS_WRITES)
    if (xs & 1) {
        setPixel(xs, y, fetchTextureColor(u, v, tetromino_type));
        xs++;
        u += du;
        v += dv;
    }

    int32_t x;
    for (x = xs; x <= xe - 1; x += 2) {
        int32_t color1 = fetchTextureColor(u, v, tetromino_type);
        u += du;
        v += dv;
        int32_t color2 = fetchTextureColor(u, v, tetromino_type);
        u += du;
        v += dv;
#ifdef BIGENDIAN_TEXTURING
        int32_t colors = (color1 << 8) | color2;
#else
        int32_t colors = (color2 << 8) | color1;
#endif
        SetPixel16(x, y, colors);
    }
    if (x == xe) {
        int32_t color = fetchTextureColor(u, v, tetromino_type);
        setPixel(x, y, color);
    }
#endif
}

void drawTexturedTriangle(Point2D p1, Point2D p2, Point2D p3, int tetromino_type) {
    // Sort vertices by y-coordinate ascending (p1.y <= p2.y <= p3.y)
    if (p1.y > p2.y) { Point2D temp = p1; p1 = p2; p2 = temp; }
    if (p1.y > p3.y) { Point2D temp = p1; p1 = p3; p3 = temp; }
    if (p2.y > p3.y) { Point2D temp = p2; p2 = p3; p3 = temp; }

    int32_t total_height = p3.y - p1.y;
    if (total_height == 0) return;

    // For fixed-point calculations
    int32_t dx13 = ((p3.x - p1.x) << FIXED_POINT_SHIFT) / total_height;
    int32_t du13 = ((int32_t)(p3.u - p1.u)) / total_height;
    int32_t dv13 = ((int32_t)(p3.v - p1.v)) / total_height;

    int32_t dx12 = 0, du12 = 0, dv12 = 0;
    int32_t dx23 = 0, du23 = 0, dv23 = 0;

    int32_t segment_height = p2.y - p1.y;
    if (segment_height > 0) {
        dx12 = ((p2.x - p1.x) << FIXED_POINT_SHIFT) / segment_height;
        du12 = ((int32_t)(p2.u - p1.u)) / segment_height;
        dv12 = ((int32_t)(p2.v - p1.v)) / segment_height;
    }

    if (p3.y - p2.y > 0) {
        dx23 = ((p3.x - p2.x) << FIXED_POINT_SHIFT) / (p3.y - p2.y);
        du23 = ((int32_t)(p3.u - p2.u)) / (p3.y - p2.y);
        dv23 = ((int32_t)(p3.v - p2.v)) / (p3.y - p2.y);
    }

    int32_t x_start = p1.x << FIXED_POINT_SHIFT;
    int32_t u_start = p1.u;
    int32_t v_start = p1.v;

    int32_t x_end = x_start;
    int32_t u_end = u_start;
    int32_t v_end = v_start;

    int32_t y;

    // First half
    for (y = p1.y; y < p2.y; y++) {
        int32_t xs = x_start >> FIXED_POINT_SHIFT;
        int32_t xe = x_end >> FIXED_POINT_SHIFT;

        int32_t us = u_start;
        int32_t vs = v_start;

        int32_t ue = u_end;
        int32_t ve = v_end;

        if (xs > xe) {
            int32_t temp_x = xs; xs = xe; xe = temp_x;
            int32_t temp_u = us; us = ue; ue = temp_u;
            int32_t temp_v = vs; vs = ve; ve = temp_v;
        }

        int32_t dx = xe - xs;
        int32_t du = 0, dv = 0;
        if (dx > 0) {
            du = (ue - us) / dx;
            dv = (ve - vs) / dx;
        }

        drawScanline(xs, xe, us, vs, du, dv, y, tetromino_type);

        x_start += dx13;
        u_start += du13;
        v_start += dv13;

        x_end += dx12;
        u_end += du12;
        v_end += dv12;
    }

    // Second half
    segment_height = p3.y - p2.y;
    if (segment_height == 0) return;

    x_end = (p2.x << FIXED_POINT_SHIFT);
    u_end = p2.u;
    v_end = p2.v;

    for (y = p2.y; y <= p3.y; y++) {
        int32_t xs = x_start >> FIXED_POINT_SHIFT;
        int32_t xe = x_end >> FIXED_POINT_SHIFT;

        int32_t us = u_start;
        int32_t vs = v_start;

        int32_t ue = u_end;
        int32_t ve = v_end;

        if (xs > xe) {
            int32_t temp_x = xs; xs = xe; xe = temp_x;
            int32_t temp_u = us; us = ue; ue = temp_u;
            int32_t temp_v = vs; vs = ve; ve = temp_v;
        }

        int32_t dx = xe - xs;
        int32_t du = 0, dv = 0;
        if (dx > 0) {
            du = (ue - us) / dx;
            dv = (ve - vs) / dx;
        }

        drawScanline(xs, xe, us, vs, du, dv, y, tetromino_type);

        x_start += dx13;
        u_start += du13;
        v_start += dv13;

        x_end += dx23;
        u_end += du23;
        v_end += dv23;
    }
}

// Rotate a point around the X-axis
Point3D rotateX(Point3D p, int angle) {
    int32_t sinA = sin_lookup[angle & ANGLE_MASK];
    int32_t cosA = cos_lookup[angle & ANGLE_MASK];

    int32_t y = (p.y * cosA - p.z * sinA) >> FIXED_POINT_SHIFT;
    int32_t z = (p.y * sinA + p.z * cosA) >> FIXED_POINT_SHIFT;
    return (Point3D){p.x, y, z};
}

// Rotate a point around the Y-axis
Point3D rotateY(Point3D p, int angle) {
    int32_t sinA = sin_lookup[angle & ANGLE_MASK];
    int32_t cosA = cos_lookup[angle & ANGLE_MASK];

    int32_t x = (p.x * cosA + p.z * sinA) >> FIXED_POINT_SHIFT;
    int32_t z = (p.z * cosA - p.x * sinA) >> FIXED_POINT_SHIFT;
    return (Point3D){x, p.y, z};
}

// Rotate a point around the Z-axis
Point3D rotateZ(Point3D p, int angle) {
    int32_t sinA = sin_lookup[angle & ANGLE_MASK];
    int32_t cosA = cos_lookup[angle & ANGLE_MASK];

    int32_t x = (p.x * cosA - p.y * sinA) >> FIXED_POINT_SHIFT;
    int32_t y = (p.x * sinA + p.y * cosA) >> FIXED_POINT_SHIFT;
    return (Point3D){x, y, p.z};
}

// Project a 3D point to 2D screen space with texture coordinates
Point2D project(Point3D p, uint32_t u, uint32_t v) {
    int32_t distance = PROJECTION_DISTANCE;
    int32_t factor = (distance << FIXED_POINT_SHIFT) / (distance - p.z);
    int32_t x = (p.x * factor) >> FIXED_POINT_SHIFT;
    int32_t y = (p.y * factor) >> FIXED_POINT_SHIFT;
    return (Point2D){x, y, u, v};
}

typedef struct {
    Point2D projected_vertices[4]; // Projected vertices of the face
    int32_t average_depth;         // Average depth for sorting
    int tetromino_type;            // Type of tetromino for texture mapping
} FaceToDraw;

FaceToDraw face_list[MAX_FACES];
int face_count = 0;

// Structure to store previous piece position
typedef struct {
    int x;
    int y;
    int type;
    int rotation;
} PreviousPieceState;

PreviousPieceState previous_piece_state;

// Macros for maximum sizes
#define MAX_PIECE_VERTICES (8 * 4) // Max 4 blocks * 8 vertices per block
#define MAX_PIECE_FACES    (6 * 4) // Max 4 blocks * 6 faces per block


// Function to draw the current piece using vertices instead of cubes
void draw_current_piece() {
    // Store current state for next frame's undraw
    previous_piece_state.x = current_piece.x;
    previous_piece_state.y = current_piece.y;
    previous_piece_state.type = current_piece.type;
    previous_piece_state.rotation = current_piece.rotation;

    int type = current_piece.type;
    int rotation = current_piece.rotation;
    int grid_x = current_piece.x;
    int grid_y = current_piece.y;
    const uint8_t *piece_shape = &puzzle_pieces[PUZZLE_PIECE_INDEX(type, rotation, 0, 0)];

    // Collect vertices and faces for the entire piece
    Point3D piece_vertices[MAX_PIECE_VERTICES]; // Max 4 blocks * 8 vertices per block
    uint32_t piece_texcoords[MAX_PIECE_VERTICES * 2];
    Face piece_faces[MAX_PIECE_FACES];          // Max 4 blocks * 6 faces per block
    int vertex_count = 0;
    int face_count_piece = 0;

    for (int i = 0; i < 4; i++) {
        const uint8_t *row = piece_shape + i * 4;
        for (int j = 0; j < 4; j++) {
            if (*(row + j)) {
                int x_offset = (grid_x + j) * CUBE_SIZE - (GRID_WIDTH * CUBE_SIZE) / 2;
                int y_offset = (GRID_HEIGHT - (grid_y + i + 1)) * CUBE_SIZE - (GRID_HEIGHT * CUBE_SIZE) / 2;
                int z_offset = STARTING_Z_OFFSET;

                // Add vertices for this block
                for (int k = 0; k < 8; k++) {
                    Point3D v = cube_vertices_template[k];
                    v.x += x_offset;
                    v.y += y_offset;
                    v.z += z_offset;
                    piece_vertices[vertex_count + k] = v;

                    // Access texture coordinates
                    piece_texcoords[(vertex_count + k) * 2 + 0] = cube_texcoords_template[k * 2 + 0];
                    piece_texcoords[(vertex_count + k) * 2 + 1] = cube_texcoords_template[k * 2 + 1];
                }

                // Add faces for this block
                for (int k = 0; k < 6; k++) {
                    Face face = cube_faces_template[k];
                    for (int l = 0; l < 4; l++) {
                        face.vertex_indices[l] += vertex_count;
                    }
                    piece_faces[face_count_piece++] = face;
                }

                vertex_count += 8;
            }
        }
    }

    // Transform and project vertices
    Point3D transformed_vertices[MAX_PIECE_VERTICES];
    Point2D projected_points[MAX_PIECE_VERTICES];
    for (int i = 0; i < vertex_count; i++) {
        Point3D v = piece_vertices[i];

        // Apply rotations if needed
        // v = rotateX(v, angle_x);
        // v = rotateY(v, angle_y);

        transformed_vertices[i] = v;

        projected_points[i] = project(v, piece_texcoords[i * 2 + 0], piece_texcoords[i * 2 + 1]);
        projected_points[i].x += SCREEN_WIDTH_HALF;
        projected_points[i].y += SCREEN_HEIGHT_HALF;
    }

    // Back-face culling and collect faces
    for (int i = 0; i < face_count_piece; i++) {
        if (face_count >= MAX_FACES) break;

        Face face = piece_faces[i];
        Point3D *v0 = &transformed_vertices[face.vertex_indices[0]];
        Point3D *v1 = &transformed_vertices[face.vertex_indices[1]];
        Point3D *v2 = &transformed_vertices[face.vertex_indices[2]];
        Point3D *v3 = &transformed_vertices[face.vertex_indices[3]]; // Added v3

        int32_t ax = v1->x - v0->x;
        int32_t ay = v1->y - v0->y;
        int32_t az = v1->z - v0->z;

        int32_t bx = v2->x - v0->x;
        int32_t by = v2->y - v0->y;
        int32_t bz = v2->z - v0->z;

        int32_t nx = ay * bz - az * by;
        int32_t ny = az * bx - ax * bz;
        int32_t nz = ax * by - ay * bx;

        if (nz > 0) continue; // Back-face culling

        FaceToDraw *face_to_draw = face_list + face_count++;
        for (int j = 0; j < 4; j++) {
            face_to_draw->projected_vertices[j] = projected_points[face.vertex_indices[j]];
        }
        face_to_draw->average_depth = (v0->z + v1->z + v2->z + v3->z) / 4; // Use all 4 vertices
        face_to_draw->tetromino_type = type;
    }
}

void undraw_previous_piece() {
    // Similar to draw_current_piece but restores background instead
    int type = previous_piece_state.type;
    int rotation = previous_piece_state.rotation;
    int grid_x = previous_piece_state.x;
    int grid_y = previous_piece_state.y;
    
    // Access the piece shape using the updated indexing macro
    const uint8_t *piece_shape = &puzzle_pieces[PUZZLE_PIECE_INDEX(type, rotation, 0, 0)];
    
    // Collect vertices for the entire piece
    Point3D piece_vertices[8 * 4]; // Max 4 blocks * 8 vertices per block
    int vertex_count = 0;

    for (int i = 0; i < 4; i++) {
        const uint8_t *row = piece_shape + i * 4;
        for (int j = 0; j < 4; j++) {
            if (*(row + j)) {
                int x_offset = (grid_x + j) * CUBE_SIZE - (GRID_WIDTH * CUBE_SIZE) / 2;
                int y_offset = (GRID_HEIGHT - (grid_y + i + 1)) * CUBE_SIZE - (GRID_HEIGHT * CUBE_SIZE) / 2;
                int z_offset = STARTING_Z_OFFSET;

                // Add vertices for this block
                for (int k = 0; k < 8; k++) {
                    Point3D v = cube_vertices_template[k];
                    v.x += x_offset;
                    v.y += y_offset;
                    v.z += z_offset;
                    piece_vertices[vertex_count + k] = v;
                }
                vertex_count += 8;
            }
        }
    }

    // Project vertices to 2D screen space
    int min_x = SCREEN_WIDTH, min_y = SCREEN_HEIGHT, max_x = 0, max_y = 0;
    for (int i = 0; i < vertex_count; i++) {
        Point3D v = piece_vertices[i];

        // Apply rotations if needed
        //v = rotateX(v, angle_x);
        //v = rotateY(v, angle_y);

        Point2D projected = project(v, 0, 0);
        projected.x += SCREEN_WIDTH_HALF;
        projected.y += SCREEN_HEIGHT_HALF;

        // Update bounding box
        if (projected.x < min_x) min_x = projected.x;
        if (projected.y < min_y) min_y = projected.y;
        if (projected.x > max_x) max_x = projected.x;
        if (projected.y > max_y) max_y = projected.y;
    }

    // Clamp bounding box to screen dimensions
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= SCREEN_WIDTH) max_x = SCREEN_WIDTH - 1;
    if (max_y >= SCREEN_HEIGHT) max_y = SCREEN_HEIGHT - 1;

    // Restore background using memcpy
    for (int y = min_y; y <= max_y; y++) {
        const uint8_t* bg_row = (uint8_t*)bg_game + y * SCREEN_WIDTH;
        uint8_t* screen_row = (uint8_t*)framebuffer_game + y * SCREEN_WIDTH;
        memcpy32(screen_row + min_x, bg_row + min_x, max_x - min_x + 1);
    }
}


// Function to draw the grid
void draw_grid() {
    for (int i = 0; i < GRID_HEIGHT; i++) {
        for (int j = 0; j < GRID_WIDTH; j++) {
            uint8_t cell_value = grid[i * GRID_WIDTH + j];
            if (cell_value) {
                int tetromino_type = cell_value - 1;
                int x = j * CUBE_SIZE;
                int y = (GRID_HEIGHT - (i + 1)) * CUBE_SIZE;

                // Collect vertices and faces for the cube
                Point3D cube_vertices[8];
                uint32_t cube_texcoords[8 * 2];
                Face cube_faces[6];

                int x_offset = x - (GRID_WIDTH * CUBE_SIZE) / 2;
                int y_offset = y - (GRID_HEIGHT * CUBE_SIZE) / 2;
                int z_offset = STARTING_Z_OFFSET;

                // Add vertices for this block
                for (int k = 0; k < 8; k++) {
                    Point3D v = cube_vertices_template[k];
                    v.x += x_offset;
                    v.y += y_offset;
                    v.z += z_offset;
                    cube_vertices[k] = v;

                    cube_texcoords[k * 2 + 0] = cube_texcoords_template[k * 2 + 0];
                    cube_texcoords[k * 2 + 1] = cube_texcoords_template[k * 2 + 1];
                }

                // Add faces for this block
                for (int k = 0; k < 6; k++) {
                    cube_faces[k] = cube_faces_template[k];
                }

                // Transform and project vertices
                Point3D transformed_vertices[8];
                Point2D projected_points[8];
                for (int l = 0; l < 8; l++) {
                    Point3D v = cube_vertices[l];

                    // Apply rotations if needed
                    //v = rotateX(v, angle_x);
                    //v = rotateY(v, angle_y);

                    transformed_vertices[l] = v;

                    projected_points[l] = project(v, cube_texcoords[l * 2 + 0], cube_texcoords[l * 2 + 1]);
                    projected_points[l].x += SCREEN_WIDTH_HALF;
                    projected_points[l].y += SCREEN_HEIGHT_HALF;
                }

                // Back-face culling and collect faces
                for (int m = 0; m < 6; m++) {
                    if (face_count >= MAX_FACES) break;

                    Face face = cube_faces[m];
                    Point3D *v0 = &transformed_vertices[face.vertex_indices[0]];
                    Point3D *v1 = &transformed_vertices[face.vertex_indices[1]];
                    Point3D *v2 = &transformed_vertices[face.vertex_indices[2]];

                    int32_t ax = v1->x - v0->x;
                    int32_t ay = v1->y - v0->y;
                    int32_t az = v1->z - v0->z;

                    int32_t bx = v2->x - v0->x;
                    int32_t by = v2->y - v0->y;
                    int32_t bz = v2->z - v0->z;

                    int32_t nz = ax * by - ay * bx;

                    if (nz > 0) continue; // Back-face culling

                    FaceToDraw *face_to_draw = face_list + face_count++;
                    for (int n = 0; n < 4; n++) {
                        face_to_draw->projected_vertices[n] = projected_points[face.vertex_indices[n]];
                    }
                    face_to_draw->average_depth = (v0->z + v1->z + v2->z +
                        transformed_vertices[face.vertex_indices[3]].z) / 4;
                    face_to_draw->tetromino_type = tetromino_type;
                }
            }
        }
    }
}

// Function to check collision
bool check_collision(int new_x, int new_y, int new_rotation) {
    int type = current_piece.type;
    int rotation = new_rotation;
    const uint8_t *piece_shape = &puzzle_pieces[PUZZLE_PIECE_INDEX(type, rotation, 0, 0)];

    for (int i = 0; i < 4; i++) {
        const uint8_t *row = piece_shape + i * 4;
        for (int j = 0; j < 4; j++) {
            if (*(row + j)) {
                int x = new_x + j;
                int y = new_y + i;

                if (x < 0 || x >= GRID_WIDTH || y >= GRID_HEIGHT) return true;
                if (y >= 0 && grid[y * GRID_WIDTH + x]) return true;
            }
        }
    }
    return false;
}

// Function to clear full lines and set background flag
void clear_lines() {
    bool lines_cleared = false;
    for (int i = GRID_HEIGHT - 1; i >= 0; i--) {
        bool full = true;
        uint8_t *row = grid + i * GRID_WIDTH;
        for (int j = 0; j < GRID_WIDTH; j++) {
            if (!row[j]) {
                full = false;
                break;
            }
        }
        if (full) {
            score += 100;
            lines_cleared = true;
            memcpy(grid + GRID_WIDTH, grid, i * GRID_WIDTH);
            memset(grid, 0, GRID_WIDTH);
            i++; // Re-check the same row
        }
    }
    if (lines_cleared) {
        clear_background_frames = 120; // Display clear background for 120 frames
    }
}

// Function to check almost losing condition
void check_almost_losing() {
    almost_losing = false;
    uint8_t *row = grid + 2 * GRID_WIDTH;
    for (int j = 0; j < GRID_WIDTH; j++) {
        if (row[j]) {
            almost_losing = true;
            return;
        }
    }
}

// Function to merge piece into grid
void merge_piece() {
    int type = current_piece.type;
    int rotation = current_piece.rotation;
    int grid_x = current_piece.x;
    int grid_y = current_piece.y;
    const uint8_t *piece_shape = &puzzle_pieces[PUZZLE_PIECE_INDEX(type, rotation, 0, 0)];

    for (int i = 0; i < 4; i++) {
        const uint8_t *row = piece_shape + i * 4;
        for (int j = 0; j < 4; j++) {
            if (*(row + j)) {
                int x = grid_x + j;
                int y = grid_y + i;

                if (y >= 0 && y < GRID_HEIGHT && x >= 0 && x < GRID_WIDTH) {
                    grid[y * GRID_WIDTH + x] = type + 1;
                }
            }
        }
    }

    // Check for game over condition (if any block reaches the top row)
    uint8_t *top_row = grid;
    for (int j = 0; j < GRID_WIDTH; j++) {
        if (top_row[j]) {
            game_over = true;
            break;
        }
    }

    // Clear lines and set background if lines are cleared
    clear_lines();
    //check_almost_losing();
}

// Function to draw text
void PrintText(const char* str, int x, int y) {
    print_string(str, 255, 0, x, y, (uint8_t*)framebuffer_game);
}

// Comparator function for qsort
int compare_faces(const void *a, const void *b) {
    const FaceToDraw *faceA = (const FaceToDraw *)a;
    const FaceToDraw *faceB = (const FaceToDraw *)b;
    return faceA->average_depth - faceB->average_depth; // Sort from farthest to nearest
}

// Game States
typedef enum {
    GAME_STATE_TITLE,
    GAME_STATE_MENU,
    GAME_STATE_ARCADE,
    GAME_STATE_PUZZLE,
    GAME_STATE_GAME_OVER_ARCADE,
    GAME_STATE_GAME_OVER_PUZZLE,
    GAME_STATE_CREDITS
} GameState;

GameState game_state = GAME_STATE_TITLE;

// Uninitiliazed memory
static unsigned long int next;

static inline int myrand(void)
{
    next = next * 1103515245 + 12345;
    return (unsigned int)(next/65536) % 32768;
}

static inline void srand(unsigned int seed)
{
    next = seed;
}

// Function to spawn a new piece in Arcade Mode
void spawn_piece() {
    current_piece.type = myrand() % 7;
    current_piece.rotation = 0;
    current_piece.x = GRID_WIDTH / 2 - 2;
    current_piece.y = -4;

    clear_background_frames = 0;

    if (check_collision(current_piece.x, current_piece.y, current_piece.rotation)) {
        game_state = GAME_STATE_GAME_OVER_ARCADE;
    }
}

// Puzzle definitions
#define MAX_PUZZLES 1 // Adjust as needed

typedef struct {
    uint8_t initial_grid[GRID_HEIGHT * GRID_WIDTH];
    int num_pieces;
    int piece_sequence[10]; // Adjust size as needed
} Puzzle;

Puzzle puzzles[MAX_PUZZLES];
int current_puzzle_index = 0;
int puzzle_piece_index = 0;

// Function to initialize puzzles
void initialize_puzzles() {
    int puzzle_index = 0;

    // Puzzle 1
    memset(&puzzles[puzzle_index], 0, sizeof(Puzzle));
    // Fill all but two columns in the bottom two rows
    for (int i = GRID_HEIGHT - 2; i < GRID_HEIGHT; i++) {
        for (int j = 0; j < GRID_WIDTH; j++) {
            if (j != 5 && j != 6) { // Leave two columns empty
                puzzles[puzzle_index].initial_grid[i * GRID_WIDTH + j] = 2; // Use 'O' tetromino type (index 1)
            }
        }
    }
    puzzles[puzzle_index].num_pieces = 1;
    puzzles[puzzle_index].piece_sequence[0] = 1; // Give an 'O' piece to clear the two rows
    puzzle_index++;

    // Additional puzzles can be added here...
}


// Function to spawn a new piece in Puzzle Mode
void spawn_piece_puzzle() {
    Puzzle *puzzle = &puzzles[current_puzzle_index];
    if (puzzle_piece_index >= puzzle->num_pieces) {
        // No more pieces; check if puzzle is solved
        bool puzzle_solved = true;
        for (int i = 0; i < GRID_HEIGHT * GRID_WIDTH; i++) {
            if (grid[i]) {
                puzzle_solved = false;
                break;
            }
        }

        if (puzzle_solved) {
            // Load next puzzle
            current_puzzle_index++;
            load_puzzle(current_puzzle_index);
        } else {
            // Puzzle failed
            Game_Switch(GAME_STATE_GAME_OVER_PUZZLE); 
        }
        return;
    }

    current_piece.type = puzzle->piece_sequence[puzzle_piece_index++];
    current_piece.rotation = 0;
    current_piece.x = GRID_WIDTH / 2 - 2;
    current_piece.y = -4;

    clear_background_frames = 0;

    if (check_collision(current_piece.x, current_piece.y, current_piece.rotation)) {
        Game_Switch(GAME_STATE_GAME_OVER_PUZZLE); 
    }
}


// Function to load a puzzle
void load_puzzle(int index) {
	backtitle = false;
    if (index >= MAX_PUZZLES) {
        // No more puzzles
        Game_Switch(GAME_STATE_TITLE);
        backtitle = true;
        return;
    }

    Puzzle *puzzle = &puzzles[index];
    memcpy(grid, puzzle->initial_grid, sizeof(grid));
    puzzle_piece_index = 0;
    score = 0;
    game_over = false;
    clear_background_frames = 0;
    almost_losing = false;
    spawn_piece_puzzle();
    return;
}

// Define the texture coordinates for the cube vertices (one-dimensional array)
const uint32_t cube_texcoords[48] = {
    // Front face (vertices 0-3)
    0 << 8,  0 << 8,  // Vertex 0
    31 << 8, 0 << 8,  // Vertex 1
    31 << 8, 31 << 8, // Vertex 2
    0 << 8, 31 << 8,  // Vertex 3

    // Back face (vertices 4-7)
    0 << 8,  0 << 8,  // Vertex 4
    31 << 8, 0 << 8,  // Vertex 5
    31 << 8, 31 << 8, // Vertex 6
    0 << 8, 31 << 8,  // Vertex 7

    // Left face (vertices 8-11)
    0 << 8,  0 << 8,  // Vertex 8
    31 << 8, 0 << 8,  // Vertex 9
    31 << 8, 31 << 8, // Vertex10
    0 << 8, 31 << 8,  // Vertex11

    // Right face (vertices 12-15)
    0 << 8,  0 << 8,  // Vertex12
    31 << 8, 0 << 8,  // Vertex13
    31 << 8, 31 << 8, // Vertex14
    0 << 8, 31 << 8,  // Vertex15

    // Top face (vertices 16-19)
    0 << 8,  0 << 8,  // Vertex16
    31 << 8, 0 << 8,  // Vertex17
    31 << 8, 31 << 8, // Vertex18
    0 << 8, 31 << 8,  // Vertex19

    // Bottom face (vertices 20-23)
    0 << 8,  0 << 8,  // Vertex20
    31 << 8, 0 << 8,  // Vertex21
    31 << 8, 31 << 8, // Vertex22
    0 << 8, 31 << 8   // Vertex23
};

// Define the faces of the cube
const Face cube_faces[6] = {
    { { 0,  1,  2,  3} },  // Front face
    { { 4,  5,  6,  7} },  // Back face
    { { 8,  9, 10, 11} },  // Left face
    { {12, 13, 14, 15} },  // Right face
    { {16, 17, 18, 19} },  // Top face
    { {20, 21, 22, 23} }   // Bottom face
};

// Define cube vertices (global constant array)
const Point3D cube_vertices[24] = {
    // Front face
    { -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex 0
    {  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex 1
    {  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex 2
    { -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex 3

    // Back face
    {  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex 4
    { -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex 5
    { -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex 6
    {  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex 7

    // Left face
    { -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex 8
    { -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex 9
    { -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex10
    { -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex11

    // Right face
    {  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex12
    {  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex13
    {  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex14
    {  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex15

    // Top face
    { -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex16
    {  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex17
    {  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex18
    { -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex19

    // Bottom face
    { -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex20
    {  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN,  DISTANCE_CUBE_TITLESCREEN }, // Vertex21
    {  DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }, // Vertex22
    { -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN, -DISTANCE_CUBE_TITLESCREEN }  // Vertex23
};

void draw_title_cube(int angle_x, int angle_y, int angle_z, int cube_position_x, int cube_position_y, int distance_cube_titlescreen) {
    // Define cube vertices
    /*Point3D cube_vertices[24];

    // Initialize cube vertices based on the distance
    // Front face
    cube_vertices[0] = (Point3D){ -distance_cube_titlescreen,  distance_cube_titlescreen,  distance_cube_titlescreen };
    cube_vertices[1] = (Point3D){  distance_cube_titlescreen,  distance_cube_titlescreen,  distance_cube_titlescreen };
    cube_vertices[2] = (Point3D){  distance_cube_titlescreen, -distance_cube_titlescreen,  distance_cube_titlescreen };
    cube_vertices[3] = (Point3D){ -distance_cube_titlescreen, -distance_cube_titlescreen,  distance_cube_titlescreen };

    // Back face
    cube_vertices[4] = (Point3D){  distance_cube_titlescreen,  distance_cube_titlescreen, -distance_cube_titlescreen };
    cube_vertices[5] = (Point3D){ -distance_cube_titlescreen,  distance_cube_titlescreen, -distance_cube_titlescreen };
    cube_vertices[6] = (Point3D){ -distance_cube_titlescreen, -distance_cube_titlescreen, -distance_cube_titlescreen };
    cube_vertices[7] = (Point3D){  distance_cube_titlescreen, -distance_cube_titlescreen, -distance_cube_titlescreen };

    // Left face
    cube_vertices[8]  = (Point3D){ -distance_cube_titlescreen,  distance_cube_titlescreen, -distance_cube_titlescreen };
    cube_vertices[9]  = (Point3D){ -distance_cube_titlescreen,  distance_cube_titlescreen,  distance_cube_titlescreen };
    cube_vertices[10] = (Point3D){ -distance_cube_titlescreen, -distance_cube_titlescreen,  distance_cube_titlescreen };
    cube_vertices[11] = (Point3D){ -distance_cube_titlescreen, -distance_cube_titlescreen, -distance_cube_titlescreen };

    // Right face
    cube_vertices[12] = (Point3D){  distance_cube_titlescreen,  distance_cube_titlescreen,  distance_cube_titlescreen };
    cube_vertices[13] = (Point3D){  distance_cube_titlescreen,  distance_cube_titlescreen, -distance_cube_titlescreen };
    cube_vertices[14] = (Point3D){  distance_cube_titlescreen, -distance_cube_titlescreen, -distance_cube_titlescreen };
    cube_vertices[15] = (Point3D){  distance_cube_titlescreen, -distance_cube_titlescreen,  distance_cube_titlescreen };

    // Top face
    cube_vertices[16] = (Point3D){ -distance_cube_titlescreen,  distance_cube_titlescreen, -distance_cube_titlescreen };
    cube_vertices[17] = (Point3D){  distance_cube_titlescreen,  distance_cube_titlescreen, -distance_cube_titlescreen };
    cube_vertices[18] = (Point3D){  distance_cube_titlescreen,  distance_cube_titlescreen,  distance_cube_titlescreen };
    cube_vertices[19] = (Point3D){ -distance_cube_titlescreen,  distance_cube_titlescreen,  distance_cube_titlescreen };

    // Bottom face
    cube_vertices[20] = (Point3D){ -distance_cube_titlescreen, -distance_cube_titlescreen,  distance_cube_titlescreen };
    cube_vertices[21] = (Point3D){  distance_cube_titlescreen, -distance_cube_titlescreen,  distance_cube_titlescreen };
    cube_vertices[22] = (Point3D){  distance_cube_titlescreen, -distance_cube_titlescreen, -distance_cube_titlescreen };
    cube_vertices[23] = (Point3D){ -distance_cube_titlescreen, -distance_cube_titlescreen, -distance_cube_titlescreen };*/

    int32_t zOffset = distance_cube_titlescreen;
    int32_t xOffset = cube_position_x; // Use the passed cube_position_x
    int32_t yOffset = cube_position_y; // Use the passed cube_position_y

    // Prepare transformed vertices
    int num_faces = 6;
    int num_vertices = num_faces * 4; // 24 vertices
    Point3D transformed_vertices[24];
    Point2D projected_points[24];

    for (int i = 0; i < num_vertices; i++) {
        Point3D v = cube_vertices[i];

        // Rotate cube
        v = rotateX(v, angle_x);
        v = rotateY(v, angle_y);
        v = rotateZ(v, angle_z);

        // Translate cube
        v.x += xOffset;
        v.y += yOffset;
        v.z += zOffset;

        transformed_vertices[i] = v;

        // Access cube_texcoords as a one-dimensional array
        projected_points[i] = project(v, cube_texcoords[2 * i], cube_texcoords[2 * i + 1]);
        projected_points[i].x += SCREEN_WIDTH_HALF;
        projected_points[i].y += SCREEN_HEIGHT_HALF;
    }

    // Back-face culling and depth sorting
    int32_t faceDepths[6];
    int faceOrder[6];
    bool backface[6];

    for (int i = 0; i < num_faces; i++) {
        faceOrder[i] = i; // Initialize face order

        int idx0 = cube_faces[i].vertex_indices[0];
        int idx1 = cube_faces[i].vertex_indices[1];
        int idx2 = cube_faces[i].vertex_indices[2];
        int idx3 = cube_faces[i].vertex_indices[3];

        Point3D v0 = transformed_vertices[idx0];
        Point3D v1 = transformed_vertices[idx1];
        Point3D v2 = transformed_vertices[idx2];
        Point3D v3 = transformed_vertices[idx3];

        // Compute face normal
        int32_t ax = v1.x - v0.x;
        int32_t ay = v1.y - v0.y;
        int32_t az = v1.z - v0.z;

        int32_t bx = v2.x - v0.x;
        int32_t by = v2.y - v0.y;
        int32_t bz = v2.z - v0.z;

        int32_t nx = ay * bz - az * by;
        int32_t ny = az * bx - ax * bz;
        int32_t nz = ax * by - ay * bx;

        // Back-face culling
        backface[i] = (nz > 0);

        // Compute total depth (sum of z-values)
        faceDepths[i] = v0.z + v1.z + v2.z + v3.z;
    }

    // Sort faces from farthest to nearest (ascending z_sum)
    for (int i = 0; i < num_faces - 1; i++) {
        for (int j = i + 1; j < num_faces; j++) {
            if (faceDepths[faceOrder[i]] > faceDepths[faceOrder[j]]) {
                int temp = faceOrder[i];
                faceOrder[i] = faceOrder[j];
                faceOrder[j] = temp;
            }
        }
    }

    // Draw faces
    for (int k = 0; k < num_faces; k++) {
        int i = faceOrder[k];

        if (backface[i]) continue; // Skip back-facing faces

        int idx0 = cube_faces[i].vertex_indices[0];
        int idx1 = cube_faces[i].vertex_indices[1];
        int idx2 = cube_faces[i].vertex_indices[2];
        int idx3 = cube_faces[i].vertex_indices[3];

        // Draw two triangles per face
        drawTexturedTriangle(projected_points[idx0], projected_points[idx1], projected_points[idx2], 1); // Using tetromino_type 1
        drawTexturedTriangle(projected_points[idx0], projected_points[idx2], projected_points[idx3], 1);
    }
}

// Add these variables at the beginning of main, with the other variable declarations
int a_button = 0;
int b_button = 0;
int up_button = 0;
int down_button = 0;
int left_button = 0;
int right_button = 0;
int start_button = 0;
int select_button = 0;
int hold_down_button = 0;

// Previous button states
int prev_a_button = 0;
int prev_b_button = 0;
int prev_up_button = 0;
int prev_down_button = 0;
int prev_left_button = 0;
int prev_right_button = 0;
int prev_start_button = 0;
int prev_select_button = 0;

// Helper macros to detect button presses
#define BUTTON_PRESSED(b) ((b) && !(prev_##b))

// At the beginning of each frame, update previous button states
void update_previous_buttons() {
	hold_down_button = 0;

    // Store the previous frame's input state
    oldpad1 = newpad1;
    oldpad0 = newpad0;

    // Update the current input state
    newpad1 = IO_CONTROLLER1;
    newpad0 = IO_CONTROLLER0;
	
    prev_a_button = a_button;
    prev_b_button = b_button;
    prev_up_button = up_button;
    prev_down_button = down_button;
    prev_left_button = left_button;
    prev_right_button = right_button;
    prev_start_button = start_button;
    prev_select_button = select_button;
}

int title_cube_rotation_x = 0;
int title_cube_rotation_y = 0;
int title_cube_rotation_z = 0;
// Variables to control cube movement
int title_cube_position_x = 0;
int title_cube_position_z = 0;
int z_counter = 0;
int title_cube_move_direction = 1;
int title_cube_move_speed = 1; // Adjust the speed as needed
int title_cube_max_position = 64; // Maximum movement to the right
int title_cube_min_position = -64; // Maximum movement to the left;
int angle_x = 0; // Slight angle to see the top
int angle_y = 0;
    
int alt_state = GAME_STATE_TITLE;
void Game_Switch(int state)
{
	alt_state = state;
	switch(state)
	{
		case GAME_STATE_TITLE:
			z_counter = 0;
			title_cube_position_z = -2048;
			
			fadeOutPalette(gamepal);
			
			memcpy32(framebuffer_game, bg_title, SCREEN_WIDTH * SCREEN_HEIGHT);
			CopyFrameBuffer((int*) framebuffer_game, (int*) VDP_BITMAP_VRAM );
			BiosVsync();
				
			fadeInPalette(gamepal);
			
			PrintText("(C) Gameblabla 2024", SCREEN_WIDTH_HALF - 80, SCREEN_HEIGHT - 20);
		break;
		case GAME_STATE_MENU:
			memcpy32(framebuffer_game, bg_title, SCREEN_WIDTH * SCREEN_HEIGHT);
			// Draw menu options
			PrintText("Select Mode", SCREEN_WIDTH_HALF - 50, 160);
		break;
		case GAME_STATE_ARCADE:
			fadeOutPalette(gamepal);
			
			drop_interval = 30;
		
			// Initialize Arcade Mode
			memset(grid, 0, sizeof(grid));
			score = 0;
			game_over = false;
			clear_background_frames = 0;
			almost_losing = false;
			spawn_piece();
			force_redraw = 1;
			backtitle = 0;

			memcpy32(framebuffer_game, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
			CopyFrameBuffer((int*) framebuffer_game, (int*) VDP_BITMAP_VRAM );
			BiosVsync();
				
			fadeInPalette(gamepal);
		break;
		case GAME_STATE_PUZZLE:
			fadeOutPalette(gamepal);
			
			memcpy32(framebuffer_game, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
			CopyFrameBuffer((int*) framebuffer_game, (int*) VDP_BITMAP_VRAM );
			BiosVsync();
				
			fadeInPalette(gamepal);
			
			drop_interval = 30;
			
			// Initialize Puzzle Mode
			current_puzzle_index = 0;
			load_puzzle(current_puzzle_index);
			clear_background_frames = 0;
			force_redraw = 1;
			force_redraw_puzzle = 1;
			backtitle = 0;

			// Collect faces from grid and current piece
			draw_grid();

		break;
		case GAME_STATE_GAME_OVER_ARCADE:
			memset(framebuffer_game, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
		break;
		case GAME_STATE_GAME_OVER_PUZZLE:
			memset(framebuffer_game, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
		break;
		case GAME_STATE_CREDITS:
			fadeOutPalette(gamepal);
			memset(framebuffer_game, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
			CopyFrameBuffer((int*) framebuffer_game, (int*) VDP_BITMAP_VRAM );
			BiosVsync();
			fadeInPalette(gamepal);
		break;
	}
}

// Function to draw a partial grid around the current piece
void draw_grid_partial() {
    // Define the area around the current piece to redraw
    const int REDRAW_MARGIN = 3;  // Adjust this value based on your needs

    int start_y = max(0, current_piece.y - REDRAW_MARGIN);
    int end_y = min(GRID_HEIGHT - 1, current_piece.y + REDRAW_MARGIN);
    int start_x = max(0, current_piece.x - REDRAW_MARGIN);
    int end_x = min(GRID_WIDTH - 1, current_piece.x + REDRAW_MARGIN);

    // Only draw grid cells within the defined area
    for (int i = start_y; i <= end_y; i++) {
        for (int j = start_x; j <= end_x; j++) {
            uint8_t cell_value = grid[i * GRID_WIDTH + j];
            if (cell_value) {
                int tetromino_type = cell_value - 1;
                int x = j * CUBE_SIZE;
                int y = (GRID_HEIGHT - (i + 1)) * CUBE_SIZE;

                // Collect vertices and faces for the cube
                Point3D cube_vertices[8];
                uint32_t cube_texcoords[8 * 2];
                Face cube_faces[6];

                int x_offset = x - (GRID_WIDTH * CUBE_SIZE) / 2;
                int y_offset = y - (GRID_HEIGHT * CUBE_SIZE) / 2;
                int z_offset = STARTING_Z_OFFSET;

                // Add vertices for this block
                for (int k = 0; k < 8; k++) {
                    Point3D v = cube_vertices_template[k];
                    v.x += x_offset;
                    v.y += y_offset;
                    v.z += z_offset;
                    cube_vertices[k] = v;

                    cube_texcoords[k * 2 + 0] = cube_texcoords_template[k * 2 + 0];
                    cube_texcoords[k * 2 + 1] = cube_texcoords_template[k * 2 + 1];
                }

                // Add faces for this block
                for (int k = 0; k < 6; k++) {
                    cube_faces[k] = cube_faces_template[k];
                }

                // Transform and project vertices
                Point3D transformed_vertices[8];
                Point2D projected_points[8];
                for (int l = 0; l < 8; l++) {
                    Point3D v = cube_vertices[l];

                    // Apply rotations if needed
                    //v = rotateX(v, angle_x);
                    //v = rotateY(v, angle_y);

                    transformed_vertices[l] = v;

                    projected_points[l] = project(v, cube_texcoords[l * 2 + 0], cube_texcoords[l * 2 + 1]);
                    projected_points[l].x += SCREEN_WIDTH_HALF;
                    projected_points[l].y += SCREEN_HEIGHT_HALF;
                }

                // Back-face culling and collect faces
                for (int m = 0; m < 6; m++) {
                    if (face_count >= MAX_FACES) break;

                    Face face = cube_faces[m];
                    Point3D *v0 = &transformed_vertices[face.vertex_indices[0]];
                    Point3D *v1 = &transformed_vertices[face.vertex_indices[1]];
                    Point3D *v2 = &transformed_vertices[face.vertex_indices[2]];

                    int32_t ax = v1->x - v0->x;
                    int32_t ay = v1->y - v0->y;
                    int32_t az = v1->z - v0->z;

                    int32_t bx = v2->x - v0->x;
                    int32_t by = v2->y - v0->y;
                    int32_t bz = v2->z - v0->z;

                    int32_t nz = ax * by - ay * bx;

                    if (nz > 0) continue; // Back-face culling

                    FaceToDraw *face_to_draw = face_list + face_count++;
                    for (int n = 0; n < 4; n++) {
                        face_to_draw->projected_vertices[n] = projected_points[face.vertex_indices[n]];
                    }
                    face_to_draw->average_depth = (v0->z + v1->z + v2->z +
                        transformed_vertices[face.vertex_indices[3]].z) / 4;
                    face_to_draw->tetromino_type = tetromino_type;
                }
            }
        }
    }
}

void Init_CasioLoopy()
{
	maskInterrupts(0) ;
	
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

}

int32_t state = 1;

int GetRandom()
{
  state ^= state << 13;
  state ^= state << 17;
  state ^= state << 5;
  return state;
}

int main() 
{
	Init_CasioLoopy();

    // Initialize game state
    memset(grid, 0, sizeof(grid));
    score = 0;
    game_over = false;
    clear_background_frames = 0;
    almost_losing = false;

    // Initialize puzzles
    initialize_puzzles();

    int running = 1;
    uint32_t last_tick = 0;
    uint32_t last_drop = 0;
    uint32_t current_tick = 0;
    uint32_t game_tick = 0;

    int title_cube_rotation = 0;
    int blink_counter = 0;
    bool blink_on = true;

    // Menu variables
    int menu_selection = 0; // 0: Arcade, 1: Puzzle
    
    Game_Switch(GAME_STATE_TITLE);
    
	a_button = 0;
	b_button = 0;
	select_button = 0;
	start_button = 0;
	up_button = 0;
	left_button = 0;
	down_button = 0;
	right_button = 0;
	hold_down_button = 0;

    while (1) 
    {
		current_tick++;
		game_tick++;
		
		srand(current_tick);
		
        update_previous_buttons();
		a_button = 0;
		b_button = 0;
		select_button = 0;
		start_button = 0;
		up_button = 0;
		left_button = 0;
		down_button = 0;
		right_button = 0;
		hold_down_button = 0;

		if(((newpad0 & P1_START) && !(oldpad0 & P1_START))) 
		{
			start_button = 1;
		}
        
        if((newpad0 & P1_A) && !(oldpad0 & P1_A)) 
        {
			a_button = 1;
		}
	
        if((newpad1 & P1_LEFT) && !(oldpad1 & P1_LEFT)) 
        {
			left_button = 1;
		}
        else if((newpad1 & P1_RIGHT) && !(oldpad1 & P1_RIGHT)) 
        {
			right_button = 1;
		}
		
		if((newpad1 & P1_UP) && !(oldpad1 & P1_UP)) 
        {
			up_button = 1;
		}
        else if((newpad1 & P1_DOWN) && !(oldpad1 & P1_DOWN)) 
        {
			down_button = 1;
		}
		
		// Handle game states
        switch (game_state) 
        {
			case GAME_STATE_TITLE:
				// Update rotation angles
				title_cube_rotation_x = (title_cube_rotation_x + 1) & ANGLE_MASK;
				title_cube_rotation_y = (title_cube_rotation_y + 2) & ANGLE_MASK;
				title_cube_rotation_z = (title_cube_rotation_z + 3) & ANGLE_MASK;
				
                // Blinking text
                blink_counter = (blink_counter + 1) % 60;
                blink_on = blink_counter < 30;
                
                if (title_cube_position_z <= -320)
                {
					title_cube_position_z+=32;
				}
				else
				{
					title_cube_position_z = -288;
				}
				
				{
					// Update cube position
					title_cube_position_x += title_cube_move_direction * title_cube_move_speed;
					if (title_cube_position_x > title_cube_max_position) {
						title_cube_position_x = title_cube_max_position;
						title_cube_move_direction = -1; // Change direction to left
					} else if (title_cube_position_x < title_cube_min_position) {
						title_cube_position_x = title_cube_min_position;
						title_cube_move_direction = 1; // Change direction to right
					}	
				}
				
				memcpy32(framebuffer_game + (256 * 45), bg_title + (256 * 45), SCREEN_WIDTH * 100);
				
				// Draw rotating cube with updated position
				draw_title_cube(title_cube_rotation_x, title_cube_rotation_y, title_cube_rotation_z, title_cube_position_x, -20, title_cube_position_z);

                // Draw blinking "Press Start to play"
                if (blink_on) 
                {
					PrintText("Press Start to Play", SCREEN_WIDTH_HALF - 80, SCREEN_HEIGHT - 45);
                }
                else
                {
					memcpy32(framebuffer_game + (256 * 195/2), bg_title + (256 * 195/2), SCREEN_WIDTH * 10); // For blinking text
				}

				if (BUTTON_PRESSED(start_button) || BUTTON_PRESSED(a_button)) {
					Game_Switch(GAME_STATE_MENU);
				}

                break;
            case GAME_STATE_MENU:
				if (BUTTON_PRESSED(up_button)) {
					menu_selection--;
				}
				if (BUTTON_PRESSED(down_button)) {
					menu_selection++;
				}
				
				if (menu_selection < 0) menu_selection = 0;
				if (menu_selection > 1) menu_selection = 2;
				
                // Only update text portion
                memcpy32(framebuffer_game + (SCREEN_WIDTH * 90), bg_title + (SCREEN_WIDTH * 90), SCREEN_WIDTH * 48);

				switch(menu_selection)
				{
					case 0:
						PrintText("> Arcade Mode", SCREEN_WIDTH_HALF - 50, 180);
						PrintText("  Puzzle Mode", SCREEN_WIDTH_HALF - 50, 200);
						PrintText("  Credits", SCREEN_WIDTH_HALF - 50, 220);
					break;
					case 1:

						PrintText("  Arcade Mode", SCREEN_WIDTH_HALF - 50, 180);
						PrintText("> Puzzle Mode", SCREEN_WIDTH_HALF - 50, 200);
						PrintText("  Credits", SCREEN_WIDTH_HALF - 50, 220);
						
					break;
					case 2:

						PrintText("  Arcade Mode", SCREEN_WIDTH_HALF - 50, 180);
						PrintText("  Puzzle Mode", SCREEN_WIDTH_HALF - 50, 200);
						PrintText("> Credits", SCREEN_WIDTH_HALF - 50, 220);
						
					break;
				}

				if (BUTTON_PRESSED(a_button)) 
				{
					if (menu_selection == 0) {
						Game_Switch(GAME_STATE_ARCADE);
					} else if (menu_selection == 1) {
						Game_Switch(GAME_STATE_PUZZLE);
					} else if (menu_selection == 2) {
						Game_Switch(GAME_STATE_CREDITS);
					}
				}
                
                break;
            case GAME_STATE_ARCADE:
            case GAME_STATE_PUZZLE:
				int previous_piece_x = current_piece.x;
				int previous_piece_y = current_piece.y;
            
				/*if (BUTTON_PRESSED(a_button)) {
					if (game_over) {
						Game_Switch(GAME_STATE_TITLE);
					}
				}*/
				

                // Reset face count before collecting faces
                face_count = 0;
				
				if (!game_over) 
				{
					if (BUTTON_PRESSED(right_button)) {
						if (!check_collision(current_piece.x - 1, current_piece.y, current_piece.rotation))
						{
							current_piece.x--; // Its reversed due to how pieces are drawn
							force_redraw = 1;
						}
					}
					else if (BUTTON_PRESSED(left_button)) {
						if (!check_collision(current_piece.x + 1, current_piece.y, current_piece.rotation))
						{
							current_piece.x++; // Its reversed due to how pieces are drawn
							force_redraw = 1;
						}
					}
					if (BUTTON_PRESSED(down_button)) {
						if (!check_collision(current_piece.x, current_piece.y + 1, current_piece.rotation))
						{
							current_piece.y++;
							force_redraw = 1;
						}
					}
					if (BUTTON_PRESSED(a_button)) {
						int new_rotation = (current_piece.rotation + 1) & 3;
						if (!check_collision(current_piece.x, current_piece.y, new_rotation))
						{
							current_piece.rotation = new_rotation;
							force_redraw = 1;
						}
					}

            
					// Automatic drop
					if (game_tick > drop_interval) 
					{
						last_drop = game_tick;
						game_tick = 0;
						if (!check_collision(current_piece.x, current_piece.y + 1, current_piece.rotation)) {
							current_piece.y++;
							force_redraw = 1;
						} 
						else 
						{
							merge_piece();
							if (game_over) 
							{
								if (game_state == GAME_STATE_ARCADE) {
									Game_Switch(GAME_STATE_GAME_OVER_ARCADE);
								} else if (game_state == GAME_STATE_PUZZLE) {
									Game_Switch(GAME_STATE_GAME_OVER_PUZZLE);
								}
							} 
							else 
							{
								memcpy32(framebuffer_game, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
								clear_lines();
								
								if (drop_interval > 5)
								{
									drop_interval -= 1;
								}

								// After merging and clearing lines, check almost losing
								//check_almost_losing();
								if (game_state == GAME_STATE_ARCADE) 
								{
									spawn_piece();
								} else if (game_state == GAME_STATE_PUZZLE) 
								{
									spawn_piece_puzzle();
								}
								
								//force_redraw = 1;
								force_redraw_puzzle = 1;
							}
							


							if (backtitle == 0)
							{
								// Collect faces from grid and current piece
								force_redraw = 1;
							}
						}
					}
                
				}


				// Before calling the functions
				if (force_redraw) 
				{
#ifdef FORCE_FULLSCREEN_DRAWS // For platforms that are fast enough, this can avoid some visual glitches
					memcpy32(framebuffer_game, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
					draw_current_piece();
					draw_grid();
#else
					undraw_previous_piece();
					draw_current_piece();
					if (force_redraw_puzzle)
					{
						draw_grid();
					}
					else
					{
						// Only update the grid if the piece has moved
						draw_grid_partial();
					}
			
#endif
					// Sort faces
					qsort(face_list, face_count, sizeof(FaceToDraw), compare_faces);
					
					// Draw sorted faces
					for (int i = 0; i < face_count; i++) 
					{
						FaceToDraw *face = &face_list[i];

						// Draw two triangles per face
						drawTexturedTriangle(
							face->projected_vertices[0],
							face->projected_vertices[1],
							face->projected_vertices[2],
							face->tetromino_type
						);
						drawTexturedTriangle(
							face->projected_vertices[0],
							face->projected_vertices[2],
							face->projected_vertices[3],
							face->tetromino_type
						);
					}
					

					if (game_state == GAME_STATE_PUZZLE) {
						// Display remaining pieces
						int remaining_pieces = puzzles[current_puzzle_index].num_pieces - puzzle_piece_index;
						PrintText("Left", 10, 10);
						PrintText(itoa(remaining_pieces), 10, 30);
					}
					else
					{
						// Display score
						PrintText(itoa(score), 10, 10);
					}

					// Update previous position for next check
					previous_piece_x = current_piece.x;
					previous_piece_y = current_piece.y;
					force_redraw = 0;
					force_redraw_puzzle = 0;
				}




                break;
            case GAME_STATE_GAME_OVER_ARCADE:
            case GAME_STATE_GAME_OVER_PUZZLE:
				if (BUTTON_PRESSED(start_button) || BUTTON_PRESSED(a_button)) {
					Game_Switch(GAME_STATE_TITLE);
				}
            
                // Draw game over texts
                PrintText("Game Over!", SCREEN_WIDTH_HALF - 40, SCREEN_HEIGHT_HALF - 10);
                PrintText("Press Start to return", SCREEN_WIDTH_HALF - 80, SCREEN_HEIGHT_HALF + 10);

                break;
            case GAME_STATE_CREDITS:
                // Clear screen
                memset(framebuffer_game, 0, SCREEN_WIDTH * SCREEN_HEIGHT);

                // Draw game over texts
                PrintText("CREDITS", 16, 16);
                PrintText("Main Programmer : Gameblabla", 16, 32);
                PrintText("Graphics : SDXL", 16, 48);
                PrintText("I'm a proud LGBT man", 16, 64);
                
                PrintText("Imagine critizing my yaoi", 16, 128);
                PrintText("game being naughty", 16, 144);
                PrintText("and not your friends", 16, 160);
                PrintText("playing hentai games on", 16, 176);
                PrintText("PC-98. Looking at you", 16, 192);
                PrintText("VideoDojo,girlgeek", 16, 208);
                
				if (BUTTON_PRESSED(start_button) || BUTTON_PRESSED(a_button)) {
					Game_Switch(GAME_STATE_TITLE);
				}
            break;
            default:
                break;
        }
        
        CopyFrameBuffer((int*) framebuffer_game, (int*) VDP_BITMAP_VRAM );
        BiosVsync();
        
		game_state = alt_state;
    }

    return 0;
}
