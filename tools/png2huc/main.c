#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <png.h>

typedef struct tagBITMAP {
    uint16_t width;
    uint16_t height;
    uint8_t *data;
    uint8_t bytespp;
} BITMAP;

uint8_t VGA_8158_GAMEPAL[768];
uint16_t num_colors = 256;

void usage() {
    printf("PNG to RAW 8bpp big endian converter\n\n");
    printf("Usage: program_name input.png output.bin palette.pal\n");
}

uint16_t RGB2PAL(int r, int g, int b, FILE* f) {
    uint16_t ui___P;
    float fl___R = (float) r;
    float fl___G = (float) g;
    float fl___B = (float) b;

    float fl___Y = (0.2990f * fl___R) + (0.5870f * fl___G) + (0.1140f * fl___B);
    float fl___U = (-0.1686f * fl___R) - (0.3311f * fl___G) + (0.4997f * fl___B) + 128.0f;
    float fl___V = (0.4998f * fl___R) - (0.4185f * fl___G) - (0.0813f * fl___B) + 128.0f;

    fl___Y = fl___Y < 0.0f ? 0.0f : (fl___Y > 255.0f ? 255.0f : fl___Y);
    fl___U = fl___U < 0.0f ? 0.0f : (fl___U > 255.0f ? 255.0f : fl___U);
    fl___V = fl___V < 0.0f ? 0.0f : (fl___V > 255.0f ? 255.0f : fl___V);

    unsigned ui___Y = (unsigned) fl___Y;
    unsigned ui___U = (unsigned) fl___U;
    unsigned ui___V = (unsigned) fl___V;

    ui___U = (ui___U + 8) >> 4;
    ui___V = (ui___V + 8) >> 4;
    ui___P = (ui___Y << 8) + (ui___U << 4) + ui___V;

    fwrite(&ui___P, sizeof(char), 2, f);

    return ui___P;
}

static void Load_png(const char *file, BITMAP *b) {
    FILE *fp = fopen(file, "rb");
    if (!fp) {
        printf("Error opening file %s.\n", file);
        exit(1);
    }

    png_structp png_ptr;
    png_infop info_ptr;

    // Initialize libpng
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        printf("png_create_read_struct failed.\n");
        fclose(fp);
        exit(1);
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        printf("png_create_info_struct failed.\n");
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        exit(1);
    }

    // Error handling
    if (setjmp(png_jmpbuf(png_ptr))) {
        printf("Error during reading PNG file.\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    png_init_io(png_ptr, fp);

    // Read PNG info
    png_read_info(png_ptr, info_ptr);

    // Get image info
    b->width = png_get_image_width(png_ptr, info_ptr);
    b->height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    // We need 8bpp indexed color image
    if (color_type != PNG_COLOR_TYPE_PALETTE) {
        printf("PNG file is not indexed color (palette).\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    if (bit_depth != 8) {
        printf("PNG file bit depth is not 8.\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    // Read palette
    png_colorp palette;
    int num_palette;
    png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);
    num_colors = num_palette;
    if (num_colors > 256) {
        printf("Palette has more than 256 colors.\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    for (int i = 0; i < num_colors; i++) {
        VGA_8158_GAMEPAL[i * 3 + 0] = palette[i].red;
        VGA_8158_GAMEPAL[i * 3 + 1] = palette[i].green;
        VGA_8158_GAMEPAL[i * 3 + 2] = palette[i].blue;
    }

    // Read image data
    b->bytespp = 1;
    b->data = (uint8_t *)malloc(b->width * b->height);
    if (!b->data) {
        printf("Error allocating memory for image data.\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    // Read the image row by row
    png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * b->height);
    if (!row_pointers) {
        printf("Error allocating memory for row pointers.\n");
        free(b->data);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    for (int y = 0; y < b->height; y++) {
        row_pointers[y] = b->data + y * b->width;
    }

    png_read_image(png_ptr, row_pointers);

    // Clean up
    png_read_end(png_ptr, NULL);
    free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
}

int main(int argc, char *argv[]) {
    FILE *fp;
    BITMAP bmp;
    int i;

    if (argc < 4) {
        usage();
        return 0;
    }

    Load_png(argv[1], &bmp);

    // Write the palette file
    fp = fopen(argv[3], "wb");
    if (!fp) {
        printf("Cannot write to palette file.\n");
        if (bmp.data) free(bmp.data);
        return 1;
    }
    for (i = 0; i < num_colors; i++) {
        RGB2PAL(VGA_8158_GAMEPAL[(i * 3) + 0], VGA_8158_GAMEPAL[(i * 3) + 1], VGA_8158_GAMEPAL[(i * 3) + 2], fp);
    }
    fclose(fp);

    // Write the image data file (big endian format)
    fp = fopen(argv[2], "wb");
    if (!fp) {
        printf("Cannot write to output file.\n");
        if (bmp.data) free(bmp.data);
        return 1;
    }

    for (i = 0; i < (bmp.width * bmp.height); i += 2) {
        uint16_t towrite;
        if (i + 1 < (bmp.width * bmp.height)) {
            towrite = ((bmp.data[i] << 8) | (bmp.data[i + 1]));
        } else {
            towrite = (bmp.data[i] << 8);
        }
        fwrite(&towrite, sizeof(char), 2, fp);
    }

    fclose(fp);
    if (bmp.data) free(bmp.data);

    printf("Files written successfully.\n");

    return 0;
}
