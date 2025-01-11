#ifndef COMMON_H
#define COMMON_H

#ifdef BY16
#warning "Using 16-bits integers by default"
#define DEFAULT_INT int16_t
#define DEFAULT_UINT uint16_t
#else
#define DEFAULT_INT int32_t
#define DEFAULT_UINT uint32_t
#endif

extern void initDivs();

// Structure to represent a 3D point
typedef struct {
    DEFAULT_INT x, y;
    DEFAULT_INT z;
} Point3D;

// Structure to represent a 2D point with texture coordinates
typedef struct {
    DEFAULT_INT x, y;
    DEFAULT_INT u, v;
} Point2D;

// Structure to represent a face
typedef struct {
    DEFAULT_INT vertex_indices[4];
} Face;

// For sorting polygons
typedef struct {
    Point2D projected_vertices[4]; // Projected vertices of the face
    DEFAULT_INT average_depth;         // Average depth for sorting
    DEFAULT_INT tetromino_type;            // Type of tetromino for texture mapping
} FaceToDraw;

// Define a structure to hold edge data (for drawTexturedQuad)
typedef struct {
	DEFAULT_INT y_start;
	DEFAULT_INT y_end;
	int32_t x;
	DEFAULT_INT x_step;
	DEFAULT_INT u;
	DEFAULT_INT u_step;
	DEFAULT_INT v;
	DEFAULT_INT v_step;
} EdgeData;

#define DIV_TAB_SIZE 512
#define DIV_TAB_HALF DIV_TAB_SIZE/2
#define DIV_TAB_SHIFT 16
extern int32_t divTab[DIV_TAB_SIZE]; // This needs to be 32-bits

extern const int32_t puzzle_pieces[7 * 4 * 4 * 4];
extern const DEFAULT_INT cube_texcoords_template[24 * 2];
extern const DEFAULT_INT cube_faces_template[6 * 4];
extern const Point3D cube_vertices_template[8];

/* Puzzle mode specific */

// Assuming GRID_WIDTH and GRID_HEIGHT are defined somewhere
#define GRID_WIDTH 12
#define GRID_HEIGHT 12
#define MAX_PUZZLES 4 // Adjust as needed
// Macro to represent a full row of zeros
#define ROW_ZEROS 0,0,0,0,0,0,0,0,0,0,0,0

typedef struct {
    DEFAULT_INT num_pieces;
    DEFAULT_INT piece_sequence[10]; // Adjust size as needed
} Puzzle;

extern const DEFAULT_INT initial_grids[MAX_PUZZLES * GRID_HEIGHT * GRID_WIDTH];

extern int32_t MULTIPLY(DEFAULT_INT a, DEFAULT_INT b);

#endif