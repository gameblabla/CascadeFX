#ifndef CFX_BMP_WRITER_H
#define CFX_BMP_WRITER_H
#include <stdint.h>
int cfx_write_bmp8(const char *path, const uint8_t *pixels, int w, int h, const uint8_t *rgb_palette_768);
int cfx_write_png8(const char *path, const uint8_t *pixels, int w, int h, const uint8_t *rgb_palette_768);
#endif
