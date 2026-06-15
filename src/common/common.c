#include <stdint.h>
#include "common.h"
#include "defines.h"

int32_t divTab[DIV_TAB_SIZE];

int32_t MULTIPLY(DEFAULT_INT a, DEFAULT_INT b)
{
	return (DEFAULT_INT)a * (DEFAULT_INT)b;
}

void initDivs()
{
    int i, ii;
    for (i=0; i<DIV_TAB_SIZE; ++i) {
        ii = i - DIV_TAB_SIZE / 2;
        if (ii==0) ++ii;

        divTab[i] = (1 << DIV_TAB_SHIFT) / ii;
    }
}

// Puzzle pieces (shapes) as a 1D array
const int32_t puzzle_pieces[7 * 4 * 4 * 4] __attribute__((aligned(4))) = {
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


// Updated cube texture coordinates template with 24 entries
ORAM_DATA const DEFAULT_INT cube_texcoords_template[24 * 2]  __attribute__((aligned(32))) = {
    0<<8, 0<<8, 31<<8, 0<<8, 31<<8, 31<<8, 0<<8, 31<<8,
    0<<8, 0<<8, 31<<8, 0<<8, 31<<8, 31<<8, 0<<8, 31<<8,
    0<<8, 0<<8, 31<<8, 0<<8, 31<<8, 31<<8, 0<<8, 31<<8,
    0<<8, 0<<8, 31<<8, 0<<8, 31<<8, 31<<8, 0<<8, 31<<8,
    0<<8, 0<<8, 31<<8, 0<<8, 31<<8, 31<<8, 0<<8, 31<<8,
    0<<8, 0<<8, 31<<8, 0<<8, 31<<8, 31<<8, 0<<8, 31<<8
};

ORAM_DATA const DEFAULT_INT cube_faces_template[6 * 4]  __attribute__((aligned(32))) = {
    0, 1, 2, 3,   // Front face
    5, 4, 7, 6,   // Back face
    4, 0, 3, 7,   // Left face
    1, 5, 6, 2,   // Right face
    4, 5, 1, 0,   // Top face
    3, 2, 6, 7    // Bottom face
};


// Common cube data
ORAM_DATA const Point3D cube_vertices_template[8]  __attribute__((aligned(32))) = {
    {-DISTANCE_CUBE,  DISTANCE_CUBE,  DISTANCE_CUBE}, // 0
    { DISTANCE_CUBE,  DISTANCE_CUBE,  DISTANCE_CUBE}, // 1
    { DISTANCE_CUBE, -DISTANCE_CUBE,  DISTANCE_CUBE}, // 2
    {-DISTANCE_CUBE, -DISTANCE_CUBE,  DISTANCE_CUBE}, // 3
    {-DISTANCE_CUBE,  DISTANCE_CUBE, -DISTANCE_CUBE}, // 4
    { DISTANCE_CUBE,  DISTANCE_CUBE, -DISTANCE_CUBE}, // 5
    { DISTANCE_CUBE, -DISTANCE_CUBE, -DISTANCE_CUBE}, // 6
    {-DISTANCE_CUBE, -DISTANCE_CUBE, -DISTANCE_CUBE}  // 7
};


// Grids for puzzle mode
const DEFAULT_INT initial_grids[MAX_PUZZLES * GRID_HEIGHT * GRID_WIDTH]  __attribute__((aligned(4))) = {
    // Puzzle 1 grid data
    // Rows 0-9: all zeros
    ROW_ZEROS, // Row 0
    ROW_ZEROS, // Row 1
    ROW_ZEROS, // Row 2
    ROW_ZEROS, // Row 3
    ROW_ZEROS, // Row 4
    ROW_ZEROS, // Row 5
    ROW_ZEROS, // Row 6
    ROW_ZEROS, // Row 7
    ROW_ZEROS, // Row 8
    ROW_ZEROS, // Row 9
    // Row 10
    2,2,2,2,2,0,0,2,2,2,2,2,
    // Row 11
    2,2,2,2,2,0,0,2,2,2,2,2,

    // Puzzle 2 grid data
    // Rows 0-7: all zeros
    ROW_ZEROS, // Row 0
    ROW_ZEROS, // Row 1
    ROW_ZEROS, // Row 2
    ROW_ZEROS, // Row 3
    ROW_ZEROS, // Row 4
    ROW_ZEROS, // Row 5
    ROW_ZEROS, // Row 6
    ROW_ZEROS, // Row 7
    // Row 8
    1,0,0,0,1,1,1,1,1,1,1,1,
    // Row 9
    1,1,0,1,1,0,0,0,0,1,1,1,
    // Row 10
    1,1,1,1,1,1,1,1,1,1,1,1,
    // Row 11
    1,1,1,1,1,1,1,1,1,1,1,1,

    // Puzzle 3 grid data
    // Rows 0-6: all zeros
    ROW_ZEROS, // Row 0
    ROW_ZEROS, // Row 1
    ROW_ZEROS, // Row 2
    ROW_ZEROS, // Row 3
    ROW_ZEROS, // Row 4
    ROW_ZEROS, // Row 5
    ROW_ZEROS, // Row 6
    // Row 7
    0,0,0,0,1,0,0,1,1,1,1,1,
    // Row 8
    0,0,0,0,1,0,0,1,1,1,1,1,
    // Row 9
    0,0,0,0,1,1,1,1,1,1,1,1,
    // Row 10
    1,1,1,1,1,1,1,1,1,1,1,1,
    // Row 11
    1,1,1,1,1,1,1,1,1,1,1,1,

    // Puzzle 4 grid data
    // Rows 0-5: all zeros
    ROW_ZEROS, // Row 0
    ROW_ZEROS, // Row 1
    ROW_ZEROS, // Row 2
    ROW_ZEROS, // Row 3
    ROW_ZEROS, // Row 4
    ROW_ZEROS, // Row 5
    // Row 6
    1,1,1,1,0,0,0,0,1,1,1,1,
    // Row 7
    1,1,1,1,0,0,0,0,1,1,1,1,
    // Row 8
    1,1,1,1,0,0,0,0,1,1,1,1,
    // Row 9
    1,1,1,1,0,0,0,0,1,1,1,1,
    // Row 10
    1,1,1,1,1,0,0,0,1,1,1,1,
    // Row 11
    1,1,1,1,1,1,0,1,1,1,1,1,
};
