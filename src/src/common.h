#ifndef COMMON_H
#define COMMON_H

extern void initDivs();

// Structure to represent a 3D point
typedef struct {
    int32_t x, y, z;
} Point3D;

// Structure to represent a 2D point with texture coordinates
typedef struct {
    int32_t x, y;
    int32_t u, v;
} Point2D;

// Structure to represent a face
typedef struct {
    int vertex_indices[4];
} Face;

// For sorting polygons
typedef struct {
    Point2D projected_vertices[4]; // Projected vertices of the face
    int32_t average_depth;         // Average depth for sorting
    int tetromino_type;            // Type of tetromino for texture mapping
} FaceToDraw;

// Define a structure to hold edge data (for drawTexturedQuad)
typedef struct {
	int y_start, y_end;
	int32_t x, x_step;
	int32_t u, u_step;
	int32_t v, v_step;
} EdgeData;

#define DIV_TAB_SIZE 4096
#define DIV_TAB_HALF DIV_TAB_SIZE/2
#define DIV_TAB_SHIFT 16
extern int divTab[DIV_TAB_SIZE];

extern const int32_t puzzle_pieces[7 * 4 * 4 * 4];
extern const int32_t cube_texcoords_template[24 * 2];
extern const int cube_faces_template[6 * 4];
extern const Point3D cube_vertices_template[8];

/* Puzzle mode specific */

// Assuming GRID_WIDTH and GRID_HEIGHT are defined somewhere
#define GRID_WIDTH 12
#define GRID_HEIGHT 12
#define MAX_PUZZLES 4 // Adjust as needed
// Macro to represent a full row of zeros
#define ROW_ZEROS 0,0,0,0,0,0,0,0,0,0,0,0

typedef struct {
    int num_pieces;
    int piece_sequence[10]; // Adjust size as needed
} Puzzle;

extern const int initial_grids[MAX_PUZZLES * GRID_HEIGHT * GRID_WIDTH];

#endif