#include "bmp_writer.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void put16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, uint32_t v) { put16(f, (uint16_t)v); put16(f, (uint16_t)(v >> 16)); }
static void put32be(FILE *f, uint32_t v) { fputc((int)(v >> 24), f); fputc((int)(v >> 16), f); fputc((int)(v >> 8), f); fputc((int)v, f); }

int cfx_write_bmp8(const char *path, const uint8_t *pixels, int w, int h, const uint8_t *rgb)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    const int stride = (w + 3) & ~3;
    const uint32_t off = 14 + 40 + 256 * 4;
    const uint32_t size = off + (uint32_t)stride * (uint32_t)h;
    fputc('B', f); fputc('M', f); put32(f, size); put16(f, 0); put16(f, 0); put32(f, off);
    put32(f, 40); put32(f, (uint32_t)w); put32(f, (uint32_t)h); put16(f, 1); put16(f, 8);
    put32(f, 0); put32(f, (uint32_t)(stride * h)); put32(f, 2835); put32(f, 2835); put32(f, 256); put32(f, 0);
    for (int i = 0; i < 256; ++i) {
        uint8_t r = rgb ? rgb[i*3+0] : (uint8_t)i;
        uint8_t g = rgb ? rgb[i*3+1] : (uint8_t)i;
        uint8_t b = rgb ? rgb[i*3+2] : (uint8_t)i;
        fputc(b, f); fputc(g, f); fputc(r, f); fputc(0, f);
    }
    uint8_t pad[4] = {0,0,0,0};
    for (int y = h - 1; y >= 0; --y) {
        fwrite(pixels + (size_t)y * (size_t)w, 1, (size_t)w, f);
        fwrite(pad, 1, (size_t)(stride - w), f);
    }
    fclose(f);
    return 0;
}

static int png_chunk(FILE *f, const char tag[4], const uint8_t *data, uint32_t len)
{
    put32be(f, len);
    fwrite(tag, 1, 4, f);
    if (len && data) fwrite(data, 1, len, f);
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)tag, 4);
    if (len && data) crc = crc32(crc, data, len);
    put32be(f, crc);
    return ferror(f) ? -1 : 0;
}

int cfx_write_png8(const char *path, const uint8_t *pixels, int w, int h, const uint8_t *rgb)
{
    if (!pixels || w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, sizeof(sig), f);

    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16); ihdr[2] = (uint8_t)(w >> 8); ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16); ihdr[6] = (uint8_t)(h >> 8); ihdr[7] = (uint8_t)h;
    ihdr[8] = 8;   /* bit depth */
    ihdr[9] = 3;   /* indexed color */
    ihdr[10] = 0;  /* deflate */
    ihdr[11] = 0;  /* standard filters */
    ihdr[12] = 0;  /* no interlace */
    if (png_chunk(f, "IHDR", ihdr, sizeof(ihdr)) < 0) { fclose(f); return -1; }

    uint8_t plte[256 * 3];
    if (rgb) memcpy(plte, rgb, sizeof(plte));
    else {
        for (int i = 0; i < 256; ++i) plte[i*3+0] = plte[i*3+1] = plte[i*3+2] = (uint8_t)i;
    }
    if (png_chunk(f, "PLTE", plte, sizeof(plte)) < 0) { fclose(f); return -1; }

    const size_t raw_stride = (size_t)w + 1;
    const size_t raw_size = raw_stride * (size_t)h;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) { fclose(f); return -1; }
    for (int y = 0; y < h; ++y) {
        raw[(size_t)y * raw_stride] = 0; /* filter type 0 */
        memcpy(raw + (size_t)y * raw_stride + 1, pixels + (size_t)y * (size_t)w, (size_t)w);
    }

    uLongf comp_bound = compressBound((uLong)raw_size);
    uint8_t *comp = (uint8_t *)malloc((size_t)comp_bound);
    if (!comp) { free(raw); fclose(f); return -1; }
    int z = compress2(comp, &comp_bound, raw, (uLong)raw_size, Z_BEST_SPEED);
    free(raw);
    if (z != Z_OK) { free(comp); fclose(f); return -1; }
    int rc = png_chunk(f, "IDAT", comp, (uint32_t)comp_bound);
    free(comp);
    if (rc < 0) { fclose(f); return -1; }
    rc = png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    return rc;
}
