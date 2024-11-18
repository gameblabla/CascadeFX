/*
 * 2024 - Gameblabla
 * LICENSE : MIT (except when stated otherwise)
*/

#include "defines.h"

//#define CART_AUDIO 1
#define _16BITS_WRITES
//#define BIGENDIAN_TEXTURING 1
//#define FORCE_FULLSCREEN_DRAWS 1
//#define DEBUGFPS 1

#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#if PLATFORM == UNIX
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
uint8_t gamepal[768];

#define FPS_VIDEO 60
const float real_FPS = 1000/FPS_VIDEO;

void fadeInPalette(unsigned char pal[], int sizep){};
void fadeOutPalette(unsigned char pal[], int sizep){};
void Empty_Palette() {};
#endif

#if PLATFORM == NECPCFX
#include "fastking.h"
#include "pcfx.h"

#include "go_sfx.h"
#include "drop.h"
#include "selsfx.h"
#include "explosion.h"
#include "readysfx.h"

#include "gamepal.h"
#include "bg.h"
#include "textures.h"
#include "title.h"

#endif

#include "font_drawing.h"
#include "common.h"
#include "trig.h"


#ifdef CART_AUDIO

#define KRAM_PAGE0 0x00000000
#define KRAM_PAGE1 0x80000000

#define ADPCM_OFFSET 0x00000000 | KRAM_PAGE1
#define CHANNEL_0_ADPCM 0x51
#define CHANNEL_1_ADPCM 0x52
#include "music_adpcm.h"
#include "race_music.h"
#endif

#define DROP_SFX 0
#define EXPLOSION_SFX 1
#define SELECT_SFX 2
#define READY_SFX 3
#define GO_SFX 4


#if PLATFORM == NECPCFX

#define BIGENDIAN_TEXTURING 1

#define GAME_FRAMEBUFFER framebuffer_game
#define DEFAULT_INTERVAL 150
#define PLAY_SFX(channel, index) Play_PSGSample(channel, index, 0);

int16_t framebuffer_game[256*240/2];

#ifdef DEBUGFPS
#define REFRESH_SCREEN(index, length) \
	eris_king_set_kram_write(index, 1); \
	king_kram_write_buffer(framebuffer_game + index, length);
#else
#define REFRESH_SCREEN(index, length) \
	eris_king_set_kram_write(index, 1); \
	king_kram_write_buffer(framebuffer_game + index, length); 

#endif
	
#elif PLATFORM == UNIX

#define FORCE_FULLSCREEN_DRAWS 1

#define GAME_FRAMEBUFFER screen->pixels
#define REFRESH_SCREEN(index, length)

#define DEFAULT_INTERVAL 500

#define PLAY_SFX(channel, index)

// Texture data (32x224 pixels) using 8-bit color indices
uint8_t texture[32 * 224]; // 32 pixels wide, 224 pixels high

// Background data arrays
uint8_t bg_game[SCREEN_WIDTH * SCREEN_HEIGHT];
uint8_t bg_title[SCREEN_WIDTH * SCREEN_HEIGHT];

// SDL surface pointers
SDL_Surface* screen;
SDL_Surface* texture_surface;

#elif PLATFORM == CASLOOPY
#include "loopy.h"
#include "casloopy.h"


#include "bg.h"
#include "textures.h"
#include "title.h"

int16_t framebuffer_game[256*240/2];

#define BIGENDIAN_TEXTURING 1

#define GAME_FRAMEBUFFER framebuffer_game
#define REFRESH_SCREEN(index, length) CopyFrameBuffer((int*) framebuffer_game, (int*) VDP_BITMAP_VRAM ); \
        BiosVsync();

#define DEFAULT_INTERVAL 500

#define PLAY_SFX(channel, index)

#else
#error "No Platform set!"
#endif

static inline void my_memcpy32(void *dest, const void *src, size_t n) 
{
    int32_t *d = (int32_t*)dest;
    const int32_t *s = (const int32_t*)src;

    // Copy 4 bytes at a time (32 bits)
    while (n >= 4) {
        *d++ = *s++;
        n -= 4;
    }

    // Handle any remaining bytes (if the size is not a multiple of 4)
    int8_t *d8 = (int8_t*)d;
    const int8_t *s8 = (const int8_t*)s;
    while (n > 0) {
        *d8++ = *s8++;
        n--;
    }
}

static char* myitoa(int value) {
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


#include "qsort.c"
#include "defines.h"

int force_redraw = 0;
int force_redraw_puzzle = 0;
bool backtitle = false;

// Game state variables
int grid[(GRID_HEIGHT * GRID_WIDTH)];
int score = 0;
bool game_over = false;

int32_t drop_interval = 30; // Drop every 500ms

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
#define GET_PUZZLE_PIECE_INDEX(type, rotation, i, j) \
    ((((type) * 4 + (rotation)) * 4 + (i)) * 4 + (j))

FaceToDraw face_list[MAX_FACES];
int face_count = 0;

// Comparator function for qsort
static inline int compare_faces(const void *a, const void *b) {
    const FaceToDraw *faceA = (const FaceToDraw *)a;
    const FaceToDraw *faceB = (const FaceToDraw *)b;
    return faceA->average_depth - faceB->average_depth; // Sort from farthest to nearest
}

#if PLATFORM == NECPCFX
#include "draw_pcfx.c"
#elif PLATFORM == CASLOOPY
#include "draw_casloopy.c"
#else
#include "draw_pc.c"
#endif


static inline void drawTexturedQuad(Point2D p0, Point2D p1, Point2D p2, Point2D p3, int tetromino_type) 
{
    // Use the vertices as given
    Point2D points_array[4] = { p0, p1, p2, p3 };
    Point2D *points = points_array;

    // Unrolled min_y and max_y calculation
    int min_y = points->y, max_y = points->y;
    if ((points + 1)->y < min_y) min_y = (points + 1)->y; else if ((points + 1)->y > max_y) max_y = (points + 1)->y;
    if ((points + 2)->y < min_y) min_y = (points + 2)->y; else if ((points + 2)->y > max_y) max_y = (points + 2)->y;
    if ((points + 3)->y < min_y) min_y = (points + 3)->y; else if ((points + 3)->y > max_y) max_y = (points + 3)->y;
    
	//if (min_y <= 0) min_y = 0;
	//if (max_y >= SCREEN_HEIGHT) max_y = SCREEN_HEIGHT;
	
    EdgeData edges_array[4];
    EdgeData *edges = edges_array;

    // Precompute edge data and initialize edge positions in a single loop
    for (int16_t i = 0; i < 4; i++) {
        Point2D *pA = points + i;
        Point2D *pB = points + ((i + 1) & 3);  // Use bitwise AND to avoid modulo operation

        int dy = pB->y - pA->y;
        EdgeData *edge = edges + i;

        if (dy == 0) {
            edge->y_start = edge->y_end = pA->y;
            edge->x = INT_TO_FIXED(pA->x);
            edge->u = pA->u;
            edge->v = pA->v;
            edge->x_step = edge->u_step = edge->v_step = 0;
            continue; // Skip horizontal edges
        }

        if (dy > 0) {
            edge->y_start = pA->y;
            edge->y_end = pB->y;
            edge->x = INT_TO_FIXED(pA->x);
            edge->u = pA->u;
            edge->v = pA->v;
            
            int dx = pB->x - pA->x;
            int du = pB->u - pA->u;
			int dv = pB->v - pA->v;
            edge->x_step = Division(INT_TO_FIXED(pB->x - pA->x) , dy);
            edge->u_step = Division(pB->u - pA->u , dy);
            edge->v_step = Division(pB->v - pA->v , dy);

        } else {
            edge->y_start = pB->y;
            edge->y_end = pA->y;
            edge->x = INT_TO_FIXED(pB->x);
            edge->u = pB->u;
            edge->v = pB->v;

            dy = -dy; // Make dy positive
			int dx = pA->x - pB->x;
			int du = pA->u - pB->u;
			int dv = pA->v - pB->v;
			edge->x_step = Division(INT_TO_FIXED(pA->x - pB->x), dy);
            edge->u_step = Division(pA->u - pB->u , dy);
            edge->v_step = Division(pA->v - pB->v , dy);
        }
    }
	
	
    // For each scanline from min_y to max_y
    for (int y = min_y; y <= max_y; y++) {
        int num_intersections = 0;
        int x_intersections[4], u_intersections[4], v_intersections[4];
        int *x_int_ptr = x_intersections;
        int *u_int_ptr = u_intersections;
        int *v_int_ptr = v_intersections;

        // Process edges and update positions in a single loop
        for (int16_t i = 0; i < 4; i++) {
            EdgeData *edge = edges + i;
            if (y >= edge->y_start) 
            {
				if (y < edge->y_end)
				{
					// Store intersection data
					*x_int_ptr++ = FIXED_TO_INT(edge->x);
					*u_int_ptr++ = edge->u;
					*v_int_ptr++ = edge->v;
					num_intersections++;

					// Update edge positions for the next scanline
					edge->x += edge->x_step;
					edge->u += edge->u_step;
					edge->v += edge->v_step;
				}
            }
        }

        if (num_intersections != 2) {
            continue;
        }

        // Pointers to the first two intersections
        int *x0 = x_intersections;
        int *x1 = x_intersections + 1;
        int *u0 = u_intersections;
        int *u1 = u_intersections + 1;
        int *v0 = v_intersections;
        int *v1 = v_intersections + 1;

        // Sort intersections by x-coordinate
        if (*x0 > *x1) {
            SWAP(*x0, *x1);
            SWAP(*u0, *u1);
            SWAP(*v0, *v1);
        }

        // Draw span between the two intersections
        int xs = *x0;
        int xe = *x1;
        int us = *u0;
        int vs = *v0;
        int ue = *u1;
        int ve = *v1;

        int dx = xe - xs;
        if (dx == 0) continue;  // Avoid division by zero

        int du = Division(ue - us, dx);
		int dv = Division(ve - vs, dx);
		drawScanline(xs, xe, us, vs, du, dv, y, tetromino_type);
    }
}


static inline void draw_sorted_faces() 
{
	// Sort faces
	qsort_game(face_list, face_count, sizeof(FaceToDraw), compare_faces);
					
    for (int i = 0; i < face_count; i++) {
        FaceToDraw *face = &face_list[i];
        #if 0
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
        #endif
		drawTexturedQuad(
			face->projected_vertices[0],
			face->projected_vertices[1],
			face->projected_vertices[2],
			face->projected_vertices[3],
			face->tetromino_type
		);

    }
}






// Rotate a point around the X-axis
Point3D rotateX(Point3D p, int angle) {
    int32_t sinA = sin_lookup[angle & ANGLE_MASK];
    int32_t cosA = cos_lookup[angle & ANGLE_MASK];

    int32_t y = FIXED_TO_INT(p.y * cosA - p.z * sinA);
    int32_t z = FIXED_TO_INT(p.y * sinA + p.z * cosA);
    return (Point3D){p.x, y, z};
}

// Rotate a point around the Y-axis
Point3D rotateY(Point3D p, int angle) {
    int32_t sinA = sin_lookup[angle & ANGLE_MASK];
    int32_t cosA = cos_lookup[angle & ANGLE_MASK];

    int32_t x = FIXED_TO_INT(p.x * cosA + p.z * sinA);
    int32_t z = FIXED_TO_INT(p.z * cosA - p.x * sinA);
    return (Point3D){x, p.y, z};
}

// Rotate a point around the Z-axis
Point3D rotateZ(Point3D p, int angle) {
    int32_t sinA = sin_lookup[angle & ANGLE_MASK];
    int32_t cosA = cos_lookup[angle & ANGLE_MASK];

    int32_t x = FIXED_TO_INT(p.x * cosA - p.y * sinA);
    int32_t y = FIXED_TO_INT(p.x * sinA + p.y * cosA);
    return (Point3D){x, y, p.z};
}

// Project a 3D point to 2D screen space with texture coordinates
Point2D project(Point3D p, int32_t u, int32_t v) {
    int32_t distance = PROJECTION_DISTANCE;
    int32_t factor = Division(INT_TO_FIXED(distance) , (distance - p.z));
    int32_t x = FIXED_TO_INT(p.x * factor);
    int32_t y = FIXED_TO_INT(p.y * factor);
    return (Point2D){x, y, u, v};
}

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

static inline void transform_cube_block(Point3D *transformed_vertices, int x_offset, int y_offset, int z_offset /*, int angle_x, int angle_y*/) {
    for (int k = 0; k < 8; k++) {
        Point3D v = cube_vertices_template[k];
        v.x += x_offset;
        v.y += y_offset;
        v.z += z_offset;

        // Apply rotations if needed
        // v = rotateX(v, angle_x);
        // v = rotateY(v, angle_y);

        transformed_vertices[k] = v;
    }
}

static inline void process_cube_block_faces(Point3D *transformed_vertices, int tetromino_type) {
    for (int m = 0; m < 6; m++) {
        //if (face_count >= MAX_FACES) break;

        int face_indices[4];
        for (int n = 0; n < 4; n++) {
            face_indices[n] = cube_faces_template[m * 4 + n];
        }

        Point3D *v0 = &transformed_vertices[face_indices[0]];
        Point3D *v1 = &transformed_vertices[face_indices[1]];
        Point3D *v2 = &transformed_vertices[face_indices[2]];

        // Compute face normal
        int32_t ax = v1->x - v0->x;
        int32_t ay = v1->y - v0->y;
		//int32_t az = v1->z - v0->z;

        int32_t bx = v2->x - v0->x;
        int32_t by = v2->y - v0->y;
        //int32_t bz = v2->z - v0->z;

        //int32_t nx = ay * bz - az * by;
        //int32_t ny = az * bx - ax * bz;
        int32_t nz = ax * by - ay * bx;

        if (nz > 0) continue; // Back-face culling

        // Prepare projected vertices with per-face texture coordinates
        Point2D projected_points[4];
        int tex_coord_offset = m * 4 * 2;

        for (int n = 0; n < 4; n++) {
            Point3D *v = &transformed_vertices[face_indices[n]];
            int32_t u = cube_texcoords_template[tex_coord_offset + n * 2 + 0];
            int32_t v_tex = cube_texcoords_template[tex_coord_offset + n * 2 + 1];

            Point2D p = project(*v, u, v_tex);
            p.x += SCREEN_WIDTH_HALF;
            p.y += SCREEN_HEIGHT_HALF;

            projected_points[n] = p;
        }

        // Add face to the face list
        FaceToDraw *face_to_draw = face_list + face_count++;
        face_to_draw->projected_vertices[0] = projected_points[0];
        face_to_draw->projected_vertices[1] = projected_points[1];
        face_to_draw->projected_vertices[2] = projected_points[2];
        face_to_draw->projected_vertices[3] = projected_points[3];

        face_to_draw->average_depth = (v0->z + v1->z + v2->z + transformed_vertices[face_indices[3]].z) / 4;
        face_to_draw->tetromino_type = tetromino_type;
    }
}

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
    const int32_t *piece_shape = puzzle_pieces;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
            if (piece_shape[index]) {
                int x_offset = (grid_x + j) * CUBE_SIZE - (GRID_WIDTH * CUBE_SIZE) / 2;
                int y_offset = (GRID_HEIGHT - (grid_y + i + 1)) * CUBE_SIZE - (GRID_HEIGHT * CUBE_SIZE) / 2;
                int z_offset = STARTING_Z_OFFSET;

                // Transform and store vertices for this block
                Point3D transformed_vertices[8];
                transform_cube_block(transformed_vertices, x_offset, y_offset, z_offset);

                // Process faces for this block
                process_cube_block_faces(transformed_vertices, type);
            }
        }
    }
}

static inline void compute_bounding_box(Point3D *transformed_vertices, int *min_x, int *min_y, int *max_x, int *max_y) {
    for (int k = 0; k < 8; k++) {
        Point2D projected = project(transformed_vertices[k], 0, 0);
        projected.x += SCREEN_WIDTH_HALF;
        projected.y += SCREEN_HEIGHT_HALF;

        if (projected.x < *min_x) *min_x = projected.x;
        if (projected.y < *min_y) *min_y = projected.y;
        if (projected.x > *max_x) *max_x = projected.x;
        if (projected.y > *max_y) *max_y = projected.y;
    }
}


void undraw_previous_piece() {
    // Retrieve previous piece state
    int type = previous_piece_state.type;
    int rotation = previous_piece_state.rotation;
    int grid_x = previous_piece_state.x;
    int grid_y = previous_piece_state.y;

    // Access the piece shape
    const int32_t *piece_shape = puzzle_pieces;

    // Initialize bounding box
    int min_x = SCREEN_WIDTH;
    int min_y = SCREEN_HEIGHT;
    int max_x = 0;
    int max_y = 0;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
			int index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
            if (piece_shape[index]) {
                int x_offset = (grid_x + j) * CUBE_SIZE - (GRID_WIDTH * CUBE_SIZE) / 2;
                int y_offset = (GRID_HEIGHT - (grid_y + i + 1)) * CUBE_SIZE - (GRID_HEIGHT * CUBE_SIZE) / 2;
                int z_offset = STARTING_Z_OFFSET;

                // Transform and store vertices for this block
                Point3D transformed_vertices[8];
                transform_cube_block(transformed_vertices, x_offset, y_offset, z_offset /*, angle_x, angle_y*/);

                // Compute bounding box
                compute_bounding_box(transformed_vertices, &min_x, &min_y, &max_x, &max_y);
            }
        }
    }
    
#if PLATFORM == NECPCFX || PLATFORM == CASLOOPY
    min_x-= 1;
#endif
    
    // Clamp bounding box to screen dimensions
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= SCREEN_WIDTH) max_x = SCREEN_WIDTH - 1;
    if (max_y >= SCREEN_HEIGHT) max_y = SCREEN_HEIGHT - 1;

    // Restore background within the bounding box
    for (int y = min_y; y <= max_y; y++) {
        const uint8_t* bg_row = (uint8_t*)bg_game + y * SCREEN_WIDTH;
        uint8_t* screen_row = (uint8_t*)GAME_FRAMEBUFFER + y * SCREEN_WIDTH;
#if PLATFORM == NECPCFX || PLATFORM == CASLOOPY
        my_memcpy32(screen_row + min_x, bg_row + min_x, max_x - min_x + 2);
#else
#ifdef _16BITS_WRITES
        for (int x = min_x; x <= max_x + 2 /* Extra + 2 to ensure extra clearing */; x += 2) {
            int32_t index = y * SCREEN_WIDTH + x;
            int32_t pixel1 = bg_row[x];
            int32_t pixel2 = bg_row[x + 1];
            int32_t pixels = (pixel1 << 8) | pixel2;
            SetPixel16(x, y, pixels);
        }
#else
        memcpy(screen_row + min_x, bg_row + min_x, max_x - min_x + 1);
#endif
#endif // PLATFORM == NECPCFX
    }
}


void draw_grid() {
    for (int i = 0; i < GRID_HEIGHT; i++) {
        for (int j = 0; j < GRID_WIDTH; j++) {
            int cell_value = grid[i * GRID_WIDTH + j];
            if (cell_value) {
                int tetromino_type = cell_value - 1;
                int x = j * CUBE_SIZE;
                int y = (GRID_HEIGHT - (i + 1)) * CUBE_SIZE;

                int x_offset = x - (GRID_WIDTH * CUBE_SIZE) / 2;
                int y_offset = y - (GRID_HEIGHT * CUBE_SIZE) / 2;
                int z_offset = STARTING_Z_OFFSET;

                // Transform and store vertices for this block
                Point3D transformed_vertices[8];
                transform_cube_block(transformed_vertices, x_offset, y_offset, z_offset /*, angle_x, angle_y*/);

                // Process faces for this block
                process_cube_block_faces(transformed_vertices, tetromino_type);
            }
        }
    }
}

// Function to check collision
bool check_collision(int new_x, int new_y, int new_rotation) {
    int type = current_piece.type;
    int rotation = new_rotation;
    const int32_t *piece_shape = puzzle_pieces;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
			int index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
            if (piece_shape[index]) {
                uint32_t x = new_x + j;
                int y = new_y + i;

                if (x >= GRID_WIDTH || y >= GRID_HEIGHT) return true;
                if (y >= 0 && grid[y * GRID_WIDTH + x]) return true;
            }
        }
    }
    return false;
}

// Function to clear full lines and set background flag
void clear_lines() {
    for (int i = GRID_HEIGHT - 1; i >= 0; i--) {
        bool full = true;
        for (int j = 0; j < GRID_WIDTH; j++) {
            if (grid[i * GRID_WIDTH + j] == 0) {
                full = false;
                break;
            }
        }
        if (full) {
            score += 100;
            PLAY_SFX(0, EXPLOSION_SFX);
            // Shift rows down by one
            for (int k = i; k > 0; k--) {
                for (int j = 0; j < GRID_WIDTH; j++) {
                    grid[k * GRID_WIDTH + j] = grid[(k - 1) * GRID_WIDTH + j];
                }
            }
            // Clear the top row
            for (int j = 0; j < GRID_WIDTH; j++) {
                grid[j] = 0;
            }
            // Since we've shifted the rows down, re-examine this row
            i++;
        }
    }
}


// Function to check almost losing condition
void check_almost_losing() {
    almost_losing = false;
    int *row = grid + 2 * GRID_WIDTH;
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
    const int32_t *piece_shape = puzzle_pieces;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int index = GET_PUZZLE_PIECE_INDEX(type, rotation, i, j);
            if (piece_shape[index]) {
                int x = grid_x + j;
                int y = grid_y + i;

                if (y >= 0 && y < GRID_HEIGHT && x >= 0 && x < GRID_WIDTH) {
                    grid[y * GRID_WIDTH + x] = type + 1;
                }
            }
        }
    }

    // Check for game over condition (if any block reaches the top row)
    int *top_row = grid;
    for (int j = 0; j < GRID_WIDTH; j++) {
        if (top_row[j]) {
            game_over = true;
            break;
        }
    }

    // Clear lines and set background if lines are cleared
    clear_lines();
}

int32_t color = 0;

// Function to draw text
void PrintText(const char* str, int x, int y) 
{
    print_string(str, 255, 0, x, y, (uint8_t*)GAME_FRAMEBUFFER);
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
static unsigned long int mynext;

static inline int myrand(void)
{
    mynext = mynext * 1103515245 + 12345;
    return (unsigned int)(mynext/65536) % 32768;
}

static inline void mysrand(unsigned int seed)
{
    mynext = seed;
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

#include <stdint.h>
#include <string.h>


Puzzle puzzles[MAX_PUZZLES];
int current_puzzle_index = 0;
int puzzle_piece_index = 0;



// Function to initialize puzzles
void initialize_puzzles() {
    int puzzle_index;
    for (puzzle_index = 0; puzzle_index < MAX_PUZZLES; puzzle_index++) {
        // Clear the current puzzle structure
        memset(&puzzles[puzzle_index], 0, sizeof(Puzzle));
    }
	puzzles[0].num_pieces = 1;
	puzzles[0].piece_sequence[0] = 1; // 'O' piece

	puzzles[1].num_pieces = 2;
	puzzles[1].piece_sequence[0] = 2; // 'T' piece
	puzzles[1].piece_sequence[1] = 0; // 'I' piece

	puzzles[2].num_pieces = 4;
	puzzles[2].piece_sequence[0] = 0; // 'I' piece
	puzzles[2].piece_sequence[1] = 0; // 'I' piece
	puzzles[2].piece_sequence[2] = 0; // 'I' piece
	puzzles[2].piece_sequence[3] = 1; // 'O' piece

	puzzles[3].num_pieces = 5;
	puzzles[3].piece_sequence[0] = 2; // 'T' piece
	puzzles[3].piece_sequence[1] = 0; // 'I' piece
	puzzles[3].piece_sequence[2] = 0; // 'I' piece
	puzzles[3].piece_sequence[3] = 0; // 'I' piece
	puzzles[3].piece_sequence[4] = 0; // 'I' piece
}




// Function to spawn a new piece in Puzzle Mode
void spawn_piece_puzzle() {
    Puzzle *puzzle = &puzzles[current_puzzle_index];
    if (puzzles[current_puzzle_index].num_pieces - puzzle_piece_index == 0) {
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
            drop_interval = DEFAULT_INTERVAL;
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

	memcpy(
		grid,
		&initial_grids[index * GRID_HEIGHT * GRID_WIDTH],
		GRID_HEIGHT * GRID_WIDTH * sizeof(int)
	);
   // memcpy(grid, puzzle->initial_grid, sizeof(grid));
   
    puzzle_piece_index = 0;
    score = 0;
    game_over = false;
    clear_background_frames = 0;
    almost_losing = false;
    spawn_piece_puzzle();
    return;
}


void draw_title_cube(int angle_x, int angle_y, int angle_z, int cube_position_x, int cube_position_y, int distance_cube_titlescreen) {
    // Prepare transformed vertices
    Point3D transformed_vertices[8];
    Point2D projected_points[8];

    for (int i = 0; i < 8; i++) {
        Point3D v = cube_vertices_template[i];

        // Adjust the size of the cube
        v.x = (v.x * distance_cube_titlescreen) / DISTANCE_CUBE;
        v.y = (v.y * distance_cube_titlescreen) / DISTANCE_CUBE;
        v.z = (v.z * distance_cube_titlescreen) / DISTANCE_CUBE;

        // Rotate cube
        v = rotateX(v, angle_x);
        v = rotateY(v, angle_y);
        //v = rotateZ(v, angle_z);

        // Translate cube
        v.x += cube_position_x;
        v.y += cube_position_y;
        v.z += STARTING_Z_OFFSET;

        transformed_vertices[i] = v;

        // Project vertices and store the projected points
        projected_points[i] = project(v, 0, 0); // Texture coordinates will be assigned later
        projected_points[i].x += SCREEN_WIDTH_HALF;
        projected_points[i].y += SCREEN_HEIGHT_HALF;
    }

    int num_faces = 6;
    int faceDepths[6];
    int faceOrder[6];
    bool backface[6];

    // For each face, compute backface culling and depth
    for (int i = 0; i < num_faces; i++) {
        faceOrder[i] = i; // Initialize face order

        // Get indices into the vertex index mapping
        int idx0 = cube_faces_template[i * 4 + 0];
        int idx1 = cube_faces_template[i * 4 + 1];
        int idx2 = cube_faces_template[i * 4 + 2];
        int idx3 = cube_faces_template[i * 4 + 3];

        Point3D *v0 = &transformed_vertices[idx0];
        Point3D *v1 = &transformed_vertices[idx1];
        Point3D *v2 = &transformed_vertices[idx2];
        Point3D *v3 = &transformed_vertices[idx3];

        // Compute face normal (cross product)
        int32_t ax = v1->x - v0->x;
        int32_t ay = v1->y - v0->y;
        int32_t az = v1->z - v0->z;

        int32_t bx = v2->x - v0->x;
        int32_t by = v2->y - v0->y;
        int32_t bz = v2->z - v0->z;

        int32_t nz = ax * by - ay * bx;

        // Back-face culling
        backface[i] = (nz > 0);

        // Compute total depth (sum of z-values)
        faceDepths[i] = v0->z + v1->z + v2->z + v3->z;
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

        // Get indices into the vertex index mapping
        int idx0 = cube_faces_template[i * 4 + 0];
        int idx1 = cube_faces_template[i * 4 + 1];
        int idx2 = cube_faces_template[i * 4 + 2];
        int idx3 = cube_faces_template[i * 4 + 3];

        // Get texture coordinates
        int tex_coord_offset = i * 4 * 2;
        int32_t u0 = cube_texcoords_template[tex_coord_offset + 0];
        int32_t v0 = cube_texcoords_template[tex_coord_offset + 1];
        int32_t u1 = cube_texcoords_template[tex_coord_offset + 2];
        int32_t v1 = cube_texcoords_template[tex_coord_offset + 3];
        int32_t u2 = cube_texcoords_template[tex_coord_offset + 4];
        int32_t v2 = cube_texcoords_template[tex_coord_offset + 5];
        int32_t u3 = cube_texcoords_template[tex_coord_offset + 6];
        int32_t v3 = cube_texcoords_template[tex_coord_offset + 7];

        // Assign texture coordinates to projected points
        Point2D p0 = projected_points[idx0];
        p0.u = u0;
        p0.v = v0;
        Point2D p1 = projected_points[idx1];
        p1.u = u1;
        p1.v = v1;
        Point2D p2 = projected_points[idx2];
        p2.u = u2;
        p2.v = v2;
        Point2D p3 = projected_points[idx3];
        p3.u = u3;
        p3.v = v3;

        // Draw the face
        drawTexturedQuad(p0, p1, p2, p3, 1);
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


int padtype  = 0;
int paddata  = 0;
int oldpadtype  = 0;
int oldpaddata  = 0;

int32_t oldpad1, oldpad0;
int32_t newpad1, newpad0;

#if PLATFORM == NECPCFX
#define BUTTON_PRESSED(b) (b)
#define DPAD_PRESSED BUTTON_PRESSED
#elif PLATFORM == CASLOOPY
#define BUTTON_PRESSED(b) ((b) && !(prev_##b))
#define DPAD_PRESSED BUTTON_PRESSED
#else
#define BUTTON_PRESSED(b) ((b) && !(prev_##b))
#define DPAD_PRESSED BUTTON_PRESSED
#endif

// At the beginning of each frame, update previous button states
void update_previous_buttons() {
	hold_down_button = 0;

#if PLATFORM == NECPCFX
    // Store the previous frame's input state
    oldpaddata = paddata;
	padtype = eris_pad_type(0);
	paddata = eris_pad_read(0);
#elif PLATFORM == CASLOOPY
	#warning "CASIO LOOPY"
    // Store the previous frame's input state
    oldpad1 = newpad1;
    oldpad0 = newpad0;

    // Update the current input state
    newpad1 = IO_CONTROLLER1;
    newpad0 = IO_CONTROLLER0;
#endif

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

int mus = 0;

int alt_state = GAME_STATE_TITLE;
void Game_Switch(int state)
{
	alt_state = state;
#if PLATFORM == NECPCFX
	Clear_VDC(0);
#endif

	mus++;

	switch(state)
	{
		case GAME_STATE_TITLE:
			Empty_Palette();
		
			z_counter = 0;
			title_cube_position_z = 0;
			
#if PLATFORM == NECPCFX
#ifdef CART_AUDIO
			Reset_ADPCM();
			Initialize_ADPCM(ADPCM_RATE_32000);

			eris_king_set_kram_write(ADPCM_OFFSET, 1);	
			king_kram_write_buffer(musicvox, sizeof(musicvox));
			eris_low_adpcm_set_volume(1, 63, 63);
			Play_ADPCM(1, ADPCM_OFFSET, sizeof(musicvox), 1, ADPCM_RATE_8000);
#else
			cd_start_track(2);
			cd_end_track(3,CDDA_LOOP);
			eris_low_cdda_set_volume(63,63);
#endif

#elif PLATFORM == CASLOOPY
			PlayMusic(0);
#endif
			my_memcpy32(GAME_FRAMEBUFFER, bg_title, SCREEN_WIDTH * SCREEN_HEIGHT);
			REFRESH_SCREEN(0, SCREEN_WIDTH * SCREEN_HEIGHT);

			fadeInPalette(gamepal);
		break;
		case GAME_STATE_MENU:
			my_memcpy32(GAME_FRAMEBUFFER, bg_title, SCREEN_WIDTH * SCREEN_HEIGHT);
			REFRESH_SCREEN(0, SCREEN_WIDTH * SCREEN_HEIGHT);
			
			// Draw menu options
			PrintText("Select Mode", SCREEN_WIDTH_HALF - 50, 160);
		break;
		case GAME_STATE_ARCADE:
			fadeOutPalette(gamepal);
			PLAY_SFX(0, READY_SFX);
			
#if PLATFORM == NECPCFX
			eris_low_cdda_set_volume(0,0);
			//fadeOutPalette(gamepal);
#ifdef CART_AUDIO
			Reset_ADPCM();
			Initialize_ADPCM(ADPCM_RATE_32000);
			/*eris_king_set_kram_write(ADPCM_OFFSET, 1);	
			king_kram_write_buffer_bytes(race_music, sizeof(race_music));
			eris_low_adpcm_set_volume(1, 63, 63);
			Play_ADPCM(1, ADPCM_OFFSET, sizeof(race_music), 1, ADPCM_RATE_4000);*/
#else
			mus = myrand() % 2;
			if (mus > 0)
			{
				cd_start_track(3);
				cd_end_track(4,CDDA_LOOP);		
			}
			else
			{
				cd_start_track(5);
				cd_end_track(6,CDDA_LOOP);	
				mus = 0;	
			}
			eris_low_cdda_set_volume(63/2,63/2);
#endif

#elif PLATFORM == CASLOOPY
			PlayMusic(1);
#endif // NECPCFX
			
			my_memcpy32(GAME_FRAMEBUFFER, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
			REFRESH_SCREEN(0, SCREEN_WIDTH*SCREEN_HEIGHT);
			fadeInPalette(gamepal);


			drop_interval = DEFAULT_INTERVAL;
		
			// Initialize Arcade Mode
			memset(grid, 0, sizeof(grid));
			score = 0;
			game_over = false;
			clear_background_frames = 0;
			almost_losing = false;
			spawn_piece();
			force_redraw = 1;
			backtitle = 0;

			
		break;
		case GAME_STATE_PUZZLE:

			fadeOutPalette(gamepal);
#if PLATFORM == NECPCFX
			eris_low_cdda_set_volume(63/2,63/2);
			cd_start_track(4);
			cd_end_track(5,CDDA_LOOP);
#elif PLATFORM == CASLOOPY
			PlayMusic(1);
#endif
			my_memcpy32(GAME_FRAMEBUFFER, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
			REFRESH_SCREEN(0, SCREEN_WIDTH * SCREEN_HEIGHT);
			fadeInPalette(gamepal);

			drop_interval = DEFAULT_INTERVAL;
			
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
			PLAY_SFX(0, GO_SFX);
#if PLATFORM == NECPCFX
			eris_low_cdda_set_volume(0,0);
#elif PLATFORM == CASLOOPY
			PlayMusic(2);
#endif
			memset(GAME_FRAMEBUFFER, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
		break;
		case GAME_STATE_GAME_OVER_PUZZLE:
			PLAY_SFX(0, GO_SFX);
#if PLATFORM == NECPCFX
			eris_low_cdda_set_volume(0,0);
#elif PLATFORM == CASLOOPY
			PlayMusic(2);
#endif
			memset(GAME_FRAMEBUFFER, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
		break;
		case GAME_STATE_CREDITS:
			memset(GAME_FRAMEBUFFER, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
		break;
	}
}

int cubes_to_process = 0;

void draw_grid_partial() {
    // Define the area around the current piece to redraw
    const int REDRAW_MARGIN = 4;  // Adjust this value based on your needs

    int start_y = max(0, current_piece.y - REDRAW_MARGIN);
    int end_y = min(GRID_HEIGHT - 1, current_piece.y + REDRAW_MARGIN);
    int start_x = max(0, current_piece.x - REDRAW_MARGIN);
    int end_x = min(GRID_WIDTH - 1, current_piece.x + REDRAW_MARGIN);

	//cubes_to_process = 0;

    // Only draw grid cells within the defined area
    for (int i = start_y; i <= end_y; i++) {
        for (int j = start_x; j <= end_x; j++) {
            int cell_value = grid[i * GRID_WIDTH + j];
            if (cell_value) {
                int tetromino_type = cell_value - 1;
                int x = j * CUBE_SIZE;
                int y = (GRID_HEIGHT - (i + 1)) * CUBE_SIZE;

                int x_offset = x - (GRID_WIDTH * CUBE_SIZE) / 2;
                int y_offset = y - (GRID_HEIGHT * CUBE_SIZE) / 2;
                int z_offset = STARTING_Z_OFFSET;

                // Transform and store vertices for this block
                Point3D transformed_vertices[8];
                transform_cube_block(transformed_vertices, x_offset, y_offset, z_offset /*, angle_x, angle_y*/);

                // Process faces for this block
                process_cube_block_faces(transformed_vertices, tetromino_type);
                //cubes_to_process++;
            }
        }
    }
}

#if PLATFORM == UNIX
// Function to load a background into a given array
bool load_background(const char* filename, uint8_t* bg_array) {
    SDL_Surface* bg_surface = IMG_Load(filename);
    if (!bg_surface) {
        fprintf(stderr, "Unable to load %s: %s\n", filename, IMG_GetError());
        return false;
    }
    // Verify dimensions
    if (bg_surface->w != SCREEN_WIDTH || bg_surface->h != SCREEN_HEIGHT) {
        fprintf(stderr, "%s must be %d x %d pixels.\n", filename, SCREEN_WIDTH, SCREEN_HEIGHT);
        SDL_FreeSurface(bg_surface);
        return false;
    }
    // Verify 8-bit
    if (bg_surface->format->BitsPerPixel != 8) {
        fprintf(stderr, "%s must be an 8-bit image.\n", filename);
        SDL_FreeSurface(bg_surface);
        return false;
    }
    // Verify palette matches screen's palette
    if (!bg_surface->format->palette) {
        fprintf(stderr, "%s does not have a palette.\n", filename);
        SDL_FreeSurface(bg_surface);
        return false;
    }
    if (memcmp(bg_surface->format->palette->colors, screen->format->palette->colors, sizeof(SDL_Color) * screen->format->palette->ncolors) != 0) {
        fprintf(stderr, "%s palette does not match screen palette.\n", filename);
        SDL_FreeSurface(bg_surface);
        return false;
    }
    // Copy pixels
    memcpy(bg_array, bg_surface->pixels, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint8_t));
    SDL_FreeSurface(bg_surface);
    return true;
}


#endif


int Init_video_game()
{
#if PLATFORM == NECPCFX
#ifdef CART_AUDIO
	Reset_ADPCM();
	Initialize_ADPCM(ADPCM_RATE_32000);
#endif	
	
	eris_king_init();
	eris_tetsu_init();
	eris_pad_init(0);
	
	eris_low_cdda_set_volume(63,63);
	
	Set_Video(KING_BGMODE_256_PAL);
	Upload_Palette(gamepal);
#ifdef DEBUGFPS
	initTimer(0, 1423);
#else
	initTimer(1, 91);
#endif


	Load_PSGSample_RAM(drop_sfx, 0, sizeof(drop_sfx));
	Load_PSGSample_RAM(explosion_sfx, 1, sizeof(explosion_sfx));
	Load_PSGSample_RAM(select_sfx, 2, sizeof(select_sfx));
	Load_PSGSample_RAM(ready_sfx, 3, sizeof(ready_sfx));
	Load_PSGSample_RAM(go_sfx, 4, sizeof(go_sfx));
	
	cd_pausectrl(0);
#elif PLATFORM == CASLOOPY
	Init_Video_Game();

	
#else
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
    
    // Load texture
    texture_surface = IMG_Load("textures.png");
    if (!texture_surface) {
        fprintf(stderr, "Unable to load texture: %s\n", IMG_GetError());
        SDL_Quit();
        return 1;
    }

    // Ensure texture is 32x224 pixels
    if (texture_surface->w != 32 || texture_surface->h != 224) {
        fprintf(stderr, "Texture must be 32x224 pixels.\n");
        SDL_FreeSurface(texture_surface);
        SDL_Quit();
        return 1;
    }

    // Set the video mode to 8bpp and apply the texture's palette to the screen
    screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 8, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_FreeSurface(texture_surface);
        SDL_Quit();
        return 1;
    }

    // Apply the texture's palette to the screen
    if (texture_surface->format->palette) {
        SDL_SetPalette(screen, SDL_LOGPAL | SDL_PHYSPAL, texture_surface->format->palette->colors, 0, texture_surface->format->palette->ncolors);
    } else {
        fprintf(stderr, "Texture does not have a palette.\n");
        SDL_FreeSurface(texture_surface);
        SDL_Quit();
        return 1;
    }

    // Copy texture pixels (palette indices)
    memcpy(texture, texture_surface->pixels, 32 * 224 * sizeof(uint8_t));

    SDL_FreeSurface(texture_surface);

    // Load background images
    if (!load_background("background256.png", bg_game)) {
        SDL_Quit();
        return 1;
    }
    
    // Load background images
    if (!load_background("title.png", bg_title)) {
        SDL_Quit();
        return 1;
    }
#endif
	
	return 0;
}

int main() 
{
	if (Init_video_game() == 1)
	{
		return 0;
	}
	
	initDivs();

    // Initialize game state
    memset(grid, 0, sizeof(grid));
    score = 0;
    game_over = false;
    clear_background_frames = 0;
    almost_losing = false;

    // Initialize puzzles
    initialize_puzzles();
	int running = 1;
	int last_tick = 0;
	int last_drop = 0;
#if PLATFORM == UNIX
    SDL_Event event;
	last_tick = SDL_GetTicks();
	last_drop = SDL_GetTicks();
#endif

    int32_t current_tick = 0;
    int32_t game_tick = 0;

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

    while (running) 
    {
#if PLATFORM == NECPCFX || PLATFORM == CASLOOPY
		current_tick++;
		game_tick++;
		
		mysrand(current_tick);
		
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
#endif

#if PLATFORM == NECPCFX
		if ( (paddata & (1 << 8)) && !(oldpaddata & (1 << 8)))
		{
			up_button = 1;
		}
		else if ( (paddata & (1 << 10)) && !(oldpaddata & (1 << 10)))
		{
			down_button = 1;
		}
        
		if ( (paddata & (1 << 11)) && !(oldpaddata & (1 << 11)))
		{
			left_button = 1;
		}
		else if ( (paddata & (1 << 9)) && !(oldpaddata & (1 << 9)))
		{
			right_button = 1;
		}
        
		if ( (paddata & (1 << 0)) && !(oldpaddata & (1 << 0)))
        {
			a_button = 1;
		}
		
		if ( (paddata & (1 << 7)) && !(oldpaddata & (1 << 7)))
        {
			start_button = 1;
		}
		
#elif PLATFORM == CASLOOPY
		if((newpad1 & P1_UP) && !(oldpad1 & P1_UP)) 
		{
			up_button = 1;
		}
		else if((newpad1 & P1_DOWN) && !(oldpad1 & P1_DOWN)) 
		{
			down_button = 1;
		}
        
		if((newpad1 & P1_LEFT) && !(oldpad1 & P1_LEFT)) 
		{
			left_button = 1;
		}
		else if((newpad1 & P1_RIGHT) && !(oldpad1 & P1_RIGHT)) 
		{
			right_button = 1;
		}
        
		if((newpad0 & P1_A) && !(oldpad0 & P1_A)) 
        {
			a_button = 1;
		}
		
		if(((newpad0 & P1_START) && !(oldpad0 & P1_START))) 
        {
			start_button = 1;
		}
		
#else
        Uint32 current_tick = SDL_GetTicks();
        last_tick = current_tick;
        game_tick = current_tick - last_drop;
        
        update_previous_buttons();

        // Handle events
        while (SDL_PollEvent(&event)) 
        {
            switch (event.type) {
                case SDL_QUIT:
                    running = 0;
                    break;
				case SDL_KEYDOWN:
					switch (event.key.keysym.sym) {
						case SDLK_SPACE:
							a_button = 1;
							break;
						case SDLK_RETURN:
							start_button = 1;
							break;
						case SDLK_ESCAPE:
							running = 0;
							break;
						case SDLK_UP:
							up_button = 1;
							break;
						case SDLK_DOWN:
							down_button = 1;
							break;
						case SDLK_LEFT:
							left_button = 1;
							break;
						case SDLK_RIGHT:
							right_button = 1;
							break;
						case SDLK_j:
							b_button = 1;
							break;
						case SDLK_k:
							select_button = 1;
							break;
					}
					break;
				case SDL_KEYUP:
					switch (event.key.keysym.sym) {
						case SDLK_SPACE:
							a_button = 0;
							break;
						case SDLK_RETURN:
							start_button = 0;
							break;
						case SDLK_UP:
							up_button = 0;
							break;
						case SDLK_DOWN:
							down_button = 0;
							break;
						case SDLK_LEFT:
							left_button = 0;
							break;
						case SDLK_RIGHT:
							right_button = 0;
							break;
						case SDLK_j:
							b_button = 0;
							break;
						case SDLK_k:
							select_button = 0;
							break;
					}
					break;
                default:
                    break;
            }
        }
#endif
		
		// Handle game states
        switch (game_state) 
        {
			case GAME_STATE_TITLE:
				// Update rotation angles
				title_cube_rotation_x = (title_cube_rotation_x + 1) & ANGLE_MASK;
				title_cube_rotation_y = (title_cube_rotation_y + 2) & ANGLE_MASK;
				title_cube_rotation_z = (title_cube_rotation_z + 3) & ANGLE_MASK;
				
                // Blinking text
                blink_counter++;

                if (title_cube_position_z < 24)
                {
					z_counter++;
					if (z_counter > 2)
					{
						title_cube_position_z++;
						z_counter = 0;
					}
				}
				else
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

#ifdef FORCE_FULLSCREEN_DRAWS
				my_memcpy32(GAME_FRAMEBUFFER, bg_title, SCREEN_WIDTH * SCREEN_HEIGHT);
#else
				my_memcpy32(GAME_FRAMEBUFFER + (SCREEN_WIDTH * 45), bg_title + (SCREEN_WIDTH * 45), SCREEN_WIDTH * 100);
#endif
				// Draw rotating cube with updated position
				draw_title_cube(title_cube_rotation_x, title_cube_rotation_y, title_cube_rotation_z, title_cube_position_x, -20, title_cube_position_z);


				if (blink_counter < 60)
				{
					PrintText("Press Start to Play", SCREEN_WIDTH_HALF - 80, 170);
				}
				if (blink_counter >= 120)
				{
					blink_counter = 0;
				}
				
				if (BUTTON_PRESSED(start_button)) {
					Game_Switch(GAME_STATE_MENU);
				}
				
				REFRESH_SCREEN((SCREEN_WIDTH * 45), SCREEN_WIDTH*120);

				
                break;
            case GAME_STATE_MENU:
				if (BUTTON_PRESSED(up_button)) {
					PLAY_SFX(0, EXPLOSION_SFX);
					menu_selection--;
				}
				if (BUTTON_PRESSED(down_button)) {
					PLAY_SFX(0, EXPLOSION_SFX);
					menu_selection++;
				}
				
				if (menu_selection < 0) menu_selection = 0;
				if (menu_selection > 1) menu_selection = 2;
				

#ifdef FORCE_FULLSCREEN_DRAWS
				my_memcpy32(GAME_FRAMEBUFFER, bg_title, SCREEN_WIDTH * SCREEN_HEIGHT);
#else
				my_memcpy32(GAME_FRAMEBUFFER + (SCREEN_WIDTH * 90), bg_title + (SCREEN_WIDTH * 90), SCREEN_WIDTH * 48);
#endif

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

				REFRESH_SCREEN(SCREEN_WIDTH*90, SCREEN_WIDTH*48);

				if (BUTTON_PRESSED(a_button)) 
				{
					if (menu_selection == 0) 
					{
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
                // Reset face count before collecting faces
                face_count = 0;
				
				if (!game_over) 
				{
					if (DPAD_PRESSED(right_button)) {
						if (!check_collision(current_piece.x - 1, current_piece.y, current_piece.rotation))
						{
							current_piece.x--; // Its reversed due to how pieces are drawn
							force_redraw = 1;
						}
					}
					else if (DPAD_PRESSED(left_button)) {
						if (!check_collision(current_piece.x + 1, current_piece.y, current_piece.rotation))
						{
							current_piece.x++; // Its reversed due to how pieces are drawn
							force_redraw = 1;
						}
					}
					if (DPAD_PRESSED(down_button)) {
						if (!check_collision(current_piece.x, current_piece.y + 1, current_piece.rotation))
						{
							current_piece.y++;
							force_redraw = 1;
						}
						else
						{
							game_tick = drop_interval + 1;
						}
					}
					if (DPAD_PRESSED(a_button)) {
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
						last_drop = current_tick;
						game_tick = 0;
						if (!check_collision(current_piece.x, current_piece.y + 1, current_piece.rotation)) {
							current_piece.y++;
							force_redraw = 1;
						} 
						else 
						{
							PLAY_SFX(0, DROP_SFX);
							
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
								my_memcpy32(GAME_FRAMEBUFFER, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
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
					my_memcpy32(GAME_FRAMEBUFFER, bg_game, SCREEN_WIDTH * SCREEN_HEIGHT);
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
					draw_sorted_faces();
					

					if (game_state == GAME_STATE_PUZZLE) {
						// Display remaining pieces
						int remaining_pieces = puzzles[current_puzzle_index].num_pieces - puzzle_piece_index;
						PrintText("Left", 10, 10);
						PrintText(myitoa(remaining_pieces), 10, 30);
					}
					else
					{
						// Display score
						PrintText(myitoa(score), 10, 10);
					}

					REFRESH_SCREEN(0, SCREEN_WIDTH*SCREEN_HEIGHT);

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

				REFRESH_SCREEN(0, SCREEN_WIDTH*SCREEN_HEIGHT);

                break;
            case GAME_STATE_CREDITS:
                // Clear screen
                memset(GAME_FRAMEBUFFER, 0, SCREEN_WIDTH * SCREEN_HEIGHT);

                // Draw game over texts
                PrintText("CREDITS", 16, 16);
                PrintText("Main Programmer : Gameblabla", 16, 32);
                PrintText("Graphics : SDXL", 16, 48);
                PrintText("Snds: sfxr.me", 16, 64);
               
                PrintText("I'm a proud LGBT man", 16, 80);
                
                PrintText("VideoDojo:", 16, 128);
                PrintText("Pronouns:He/Him", 16, 144);
                PrintText("I dislike this gay game", 16, 160);
                PrintText("that you made!!!", 16, 176);
                
				if (BUTTON_PRESSED(start_button) || BUTTON_PRESSED(a_button)) {
					Game_Switch(GAME_STATE_TITLE);
				}
				
				REFRESH_SCREEN(0, SCREEN_WIDTH*SCREEN_HEIGHT);
            break;
            default:
                break;
        }
        


        game_state = alt_state;
        #if PLATFORM == NECPCFX
			#ifdef DEBUGFPS
			print_at(0, 1, 12, myitoa(getFps()));
			#endif
			vsync(0);
			++nframe;
		#elif PLATFORM == CASLOOPY
			CopyFrameBuffer((int*) framebuffer_game, (int*) VDP_BITMAP_VRAM );
			BiosVsync();
		#else
			if(real_FPS > SDL_GetTicks()-current_tick) SDL_Delay(real_FPS-(SDL_GetTicks()-current_tick));
			SDL_Flip(screen);
		#endif

    }

    return 0;
}
