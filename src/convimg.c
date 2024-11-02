#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <stdint.h>

static inline uint16_t swap_int16( uint16_t val )
{
    return (val << 8) | ((val >> 8) & 0xFF);
} 

void convert_to_15bit_rgb_and_write_palette(png_structp png_ptr, png_infop info_ptr, const char* palette_filename) {
    png_colorp palette;
    int num_palette;
    
    if (png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette)) {
        FILE* fp = fopen(palette_filename, "wb");
        if (!fp) {
            perror("Cannot open palette file for writing");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < num_palette; i++) {
            unsigned short color = ((palette[i].red >> 3) << 10) | ((palette[i].green >> 3) << 5) | (palette[i].blue >> 3);
            fwrite(&color, sizeof(color), 1, fp);
        }

        fclose(fp);
    }
}

void byte_swap_buffer(uint16_t* buffer, size_t count) {
    for (size_t i = 0; i < count; i++) {
        buffer[i] = (buffer[i] >> 8) | (buffer[i] << 8);
    }
}

void read_png_file(const char* filename, const char* raw_filename, const char* palette_filename, int swap_bytes) {
  
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Cannot open file for reading");
        exit(EXIT_FAILURE);
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    // Ensure 8-bit depth is used for palette images
    png_set_packing(png_ptr);

    png_uint_32 height = png_get_image_height(png_ptr, info_ptr);
    png_uint_32 rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    size_t total_size = height * rowbytes;

    // Allocate a large buffer to hold the entire image data
    uint8_t* buffer = (uint8_t*)malloc(total_size);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory for the image buffer\n");
        exit(EXIT_FAILURE);
    }

    // Copy the image data into the buffer
    for (png_uint_32 i = 0; i < height; i++) {
        uint8_t* row = (uint8_t*)malloc(rowbytes);
        if (!row) {
            fprintf(stderr, "Failed to allocate memory for the row buffer\n");
            exit(EXIT_FAILURE);
        }
        png_read_row(png_ptr, row, NULL);
        memcpy(buffer + (i * rowbytes), row, rowbytes);
        free(row); // Free the row pointer now that we've copied its data
    }

    // Perform byte swapping on the buffer if needed
    // Since this is 8bpp data, you might not need to swap bytes, but if you do:
    // Make sure you adjust the logic here to match your actual data's needs.
    

    // Perform byte swapping on the buffer if requested
    if (swap_bytes) {
        byte_swap_buffer((uint16_t*)buffer, total_size / 2);
    }

    // Write the swapped buffer to the file
    FILE *raw_fp = fopen(raw_filename, "wb");
    if (!raw_fp) {
        perror("Cannot open raw file for writing");
        exit(EXIT_FAILURE);
    }

    fwrite(buffer, 1, total_size, raw_fp);
    fclose(raw_fp);

    // Free the buffer
    free(buffer);

    // Convert and write palette
    convert_to_15bit_rgb_and_write_palette(png_ptr, info_ptr, palette_filename);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "PNG to Casio Loopy 8-bits Bitmap mode\nUsage: %s <file.png> image.bin image.pal [no_swap]\n", argv[0]);
        return EXIT_FAILURE;
    }

    int swap_bytes = 1; // Default is to swap bytes
    if (argc > 4 && strcmp(argv[4], "no_swap") == 0) {
        swap_bytes = 0; // Do not swap bytes if "no_swap" argument is provided
    }

    read_png_file(argv[1], argv[2], argv[3], swap_bytes);
    return EXIT_SUCCESS;
}

