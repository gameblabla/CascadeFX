#include <stdio.h>
#include <math.h>
#include <stdint.h>

// Fixed-point arithmetic settings
#define FIXED_POINT_SHIFT 8
#define FIXED_POINT_SCALE (1 << FIXED_POINT_SHIFT)

#define ANGLE_MAX 256
#define ANGLE_MASK (ANGLE_MAX - 1)

// Trigonometric lookup tables (scaled by FIXED_POINT_SCALE)
int32_t sin_lookup[ANGLE_MAX];
int32_t cos_lookup[ANGLE_MAX];

void initialize_trigonometry() {
    printf("#ifndef TRIGONOMETRY_LOOKUP_H\n");
    printf("#define TRIGONOMETRY_LOOKUP_H\n\n");

    printf("int16_t sin_lookup[%d] = {\n", ANGLE_MAX);
    for (int angle = 0; angle < ANGLE_MAX; angle++) {
        double radians = angle * 2.0 * M_PI / ANGLE_MAX;
        sin_lookup[angle] = (int32_t)(FIXED_POINT_SCALE * sin(radians));
        printf("    %d", sin_lookup[angle]);
        if (angle < ANGLE_MAX - 1) {
            printf(",");
        }
        printf("\n");
    }
    printf("};\n\n");

    printf("int16_t cos_lookup[%d] = {\n", ANGLE_MAX);
    for (int angle = 0; angle < ANGLE_MAX; angle++) {
        cos_lookup[angle] = (int32_t)(FIXED_POINT_SCALE * cos(angle * 2.0 * M_PI / ANGLE_MAX));
        printf("    %d", cos_lookup[angle]);
        if (angle < ANGLE_MAX - 1) {
            printf(",");
        }
        printf("\n");
    }
    printf("};\n\n");

    printf("#endif // TRIGONOMETRY_LOOKUP_H\n");
}

int main() {
    initialize_trigonometry();
    return 0;
}
