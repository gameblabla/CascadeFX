#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "defines.h"
#include "common.h"
#include "renderer3d.h"
#include "bmp_writer.h"
#include "command_input.h"
#include "input.h"
#include "trig.h"

/* Headless uses generated 8-bpp indexed assets.  The title bitmap, texture
   atlas, and palette are generated from assets/source with the same palette
   remap, so frame dumps remain coherent paletted images. */
#include "gamepal.h"
#include "textures.h"
#include "title.h"
#include "title320.h"
#include "background320.h"
#include "index_map.h"
#include "font_menudata.h"

#define TITLE_256_W 256
#define TITLE_320_W 320
#define NATIVE_H 240
#define MAX_W 320
#define MAX_H 240
#define TEX_TILE_STRIDE CFX_TEXTURE_TILE_STRIDE_BYTES

typedef struct { Point2D p; DEFAULT_INT z; } Proj;
typedef struct { int idx[4]; DEFAULT_INT z; int tile; int face_id; } FaceRef;

static uint8_t framebuffer[MAX_W * MAX_H];
static uint8_t palette_rgb[256 * 3];

/* Headless title cube state now mirrors the console title path.  The previous
   headless renderer used a separate floating-point camera, larger vertices,
   and per-frame angles in radians, which made the cube look unlike PC-FX/Loopy
   even though the palette and UVs were correct. */
typedef struct {
    DEFAULT_INT angle_x;
    DEFAULT_INT angle_y;
    DEFAULT_INT angle_z;
    DEFAULT_INT pos_x;
    DEFAULT_INT pos_z;
    DEFAULT_INT z_counter;
    DEFAULT_INT move_dir;
    DEFAULT_INT blink_counter;
} HeadlessTitleState;

static inline Point3D headless_rotate_x(Point3D p, DEFAULT_INT angle)
{
    int16_t sinA = sin_lookup[angle & ANGLE_MASK];
    int16_t cosA = cos_lookup[angle & ANGLE_MASK];
    int32_t y = FIXED_TO_INT(MULTIPLY(p.y, cosA) - MULTIPLY(p.z, sinA));
    int32_t z = FIXED_TO_INT(MULTIPLY(p.y, sinA) + MULTIPLY(p.z, cosA));
    return (Point3D){p.x, (DEFAULT_INT)y, (DEFAULT_INT)z};
}

static inline Point3D headless_rotate_y(Point3D p, DEFAULT_INT angle)
{
    int16_t sinA = sin_lookup[angle & ANGLE_MASK];
    int16_t cosA = cos_lookup[angle & ANGLE_MASK];
    int32_t x = FIXED_TO_INT(MULTIPLY(p.x, cosA) + MULTIPLY(p.z, sinA));
    int32_t z = FIXED_TO_INT(MULTIPLY(p.z, cosA) - MULTIPLY(p.x, sinA));
    return (Point3D){(DEFAULT_INT)x, p.y, (DEFAULT_INT)z};
}

static inline Point2D headless_project(Point3D p, int w, int h, DEFAULT_INT u, DEFAULT_INT v)
{
    DEFAULT_INT distance = PROJECTION_DISTANCE;
    DEFAULT_INT factor = Division(INT_TO_FIXED(distance), (distance - p.z));
    int32_t x = FIXED_TO_INT(MULTIPLY(p.x, factor));
    int32_t y = FIXED_TO_INT(MULTIPLY(p.y, factor));
    return (Point2D){(DEFAULT_INT)(x + (w >> 1)), (DEFAULT_INT)(y + (h >> 1)), u, v};
}

static void headless_title_state_init(HeadlessTitleState *s)
{
    memset(s, 0, sizeof(*s));
    s->move_dir = 1;
}

static void headless_title_state_step(HeadlessTitleState *s)
{
    s->angle_x = (s->angle_x + 1) & ANGLE_MASK;
    s->angle_y = (s->angle_y + 2) & ANGLE_MASK;
    s->angle_z = (s->angle_z + 3) & ANGLE_MASK;
    s->blink_counter++;

    if (s->pos_z < 24) {
        s->z_counter++;
        if (s->z_counter > 2) {
            s->pos_z++;
            s->z_counter = 0;
        }
    } else {
        s->pos_x += s->move_dir;
        if (s->pos_x > 64) {
            s->pos_x = 64;
            s->move_dir = -1;
        } else if (s->pos_x < -64) {
            s->pos_x = -64;
            s->move_dir = 1;
        }
    }
}

static void build_palette_rgb(void)
{
    for (int i = 0; i < 256; ++i) {
        uint16_t c = gamepal[i];
        uint8_t r5 = (uint8_t)((c >> 10) & 31);
        uint8_t g5 = (uint8_t)((c >> 5) & 31);
        uint8_t b5 = (uint8_t)(c & 31);
        palette_rgb[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        palette_rgb[i * 3 + 1] = (uint8_t)((g5 << 3) | (g5 >> 2));
        palette_rgb[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
}

static void blit_packed_title(int w, int h, const uint16_t *src, int src_w)
{
    const int copy_w = (src_w < w) ? src_w : w;
    for (int y = 0; y < NATIVE_H && y < h; ++y) {
        uint8_t *dst = framebuffer + (size_t)y * (size_t)w;
        const uint16_t *row = src + (size_t)y * (size_t)(src_w / 2);
        for (int x = 0; x < copy_w / 2; ++x) {
            uint16_t pair = row[x];
            dst[x * 2 + 0] = (uint8_t)(pair >> 8);
            dst[x * 2 + 1] = (uint8_t)(pair & 255);
        }
    }
}

static void clear_to_title(int w, int h)
{
    memset(framebuffer, cfx_palette_index_map[0x1c], (size_t)w * (size_t)h);
    if (w == TITLE_320_W) {
        /* Use the supplied 320x240 source artwork directly; do not center or
           stretch the 256-wide platform title in headless 320 mode. */
        blit_packed_title(w, h, bg_title320, TITLE_320_W);
    } else {
        blit_packed_title(w, h, bg_title, TITLE_256_W);
    }
}

static void clear_to_game(int w, int h)
{
    memset(framebuffer, cfx_palette_index_map[0x1c], (size_t)w * (size_t)h);
    if (w == TITLE_320_W) {
        blit_packed_title(w, h, bg_game320, TITLE_320_W);
    } else {
        /* The console gameplay backgrounds are 256-wide.  For the 256-pixel
           headless fallback, crop the center of the 320-wide PC showcase art. */
        for (int y = 0; y < NATIVE_H && y < h; ++y) {
            uint8_t *dst = framebuffer + (size_t)y * (size_t)w;
            const uint16_t *row = bg_game320 + (size_t)y * (TITLE_320_W / 2);
            for (int x = 0; x < w / 2; ++x) {
                uint16_t pair = row[x + 16];
                dst[x * 2 + 0] = (uint8_t)(pair >> 8);
                dst[x * 2 + 1] = (uint8_t)(pair & 255);
            }
        }
    }
}

static void plot(int w, int h, int x, int y, uint8_t c)
{
    if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) framebuffer[y * w + x] = c;
}

static const uint8_t font5x7[][5] = {
    {0,0,0,0,0},              /* space */
    {0x7e,0x11,0x11,0x11,0x7e}, /* A */
    {0x7f,0x49,0x49,0x49,0x36}, /* B */
    {0x3e,0x41,0x41,0x41,0x22}, /* C */
    {0x7f,0x41,0x41,0x22,0x1c}, /* D */
    {0x7f,0x49,0x49,0x49,0x41}, /* E */
    {0x7f,0x09,0x09,0x09,0x01}, /* F */
    {0x3e,0x41,0x49,0x49,0x7a}, /* G */
    {0x7f,0x08,0x08,0x08,0x7f}, /* H */
    {0x00,0x41,0x7f,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3f,0x01}, /* J */
    {0x7f,0x08,0x14,0x22,0x41}, /* K */
    {0x7f,0x40,0x40,0x40,0x40}, /* L */
    {0x7f,0x02,0x0c,0x02,0x7f}, /* M */
    {0x7f,0x04,0x08,0x10,0x7f}, /* N */
    {0x3e,0x41,0x41,0x41,0x3e}, /* O */
    {0x7f,0x09,0x09,0x09,0x06}, /* P */
    {0x3e,0x41,0x51,0x21,0x5e}, /* Q */
    {0x7f,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7f,0x01,0x01}, /* T */
    {0x3f,0x40,0x40,0x40,0x3f}, /* U */
    {0x1f,0x20,0x40,0x20,0x1f}, /* V */
    {0x7f,0x20,0x18,0x20,0x7f}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x3e,0x51,0x49,0x45,0x3e}, /* 0 */
    {0x00,0x42,0x7f,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4b,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7f,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3c,0x4a,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1e}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
};

static const uint8_t *glyph(char ch)
{
    if (ch == ' ') return font5x7[0];
    if (ch >= 'A' && ch <= 'Z') return font5x7[1 + ch - 'A'];
    if (ch >= '0' && ch <= '9') return font5x7[27 + ch - '0'];
    if (ch == ':') return font5x7[37];
    return font5x7[0];
}

static int is_outline_pixel8(const uint8_t *charfont, int x, int y)
{
    const uint8_t b = (uint8_t)(1u << (7 - x));
    if (charfont[y] & b) {
        return 0;
    }

    for (int oy = -1; oy <= 1; ++oy) {
        const int yy = y + oy;
        if ((unsigned)yy >= 8u) {
            continue;
        }
        for (int ox = -1; ox <= 1; ++ox) {
            if (ox == 0 && oy == 0) {
                continue;
            }
            const int xx = x + ox;
            if ((unsigned)xx >= 8u) {
                continue;
            }
            if (charfont[yy] & (uint8_t)(1u << (7 - xx))) {
                return 1;
            }
        }
    }
    return 0;
}

static void draw_text(int w, int h, int x, int y, const char *s, uint8_t fg, uint8_t outline)
{
    for (; *s; ++s) {
        if (*s == '\n') {
            y += 8;
            continue;
        }
        const uint8_t *charfont = n2DLib_font + ((uint8_t)*s * 8u);
        for (int yy = 0; yy < 8; ++yy) {
            const uint8_t row = charfont[yy];
            for (int xx = 0; xx < 8; ++xx) {
                const uint8_t bit = (uint8_t)(1u << (7 - xx));
                if (row & bit) {
                    plot(w, h, x + xx, y + yy, fg);
                } else if (is_outline_pixel8(charfont, xx, yy)) {
                    plot(w, h, x + xx, y + yy, outline);
                }
            }
        }
        x += 8;
    }
}

static void draw_title_cube(CfxRenderer3D *renderer, int w, int h, const HeadlessTitleState *title);

static void draw_panel(int w, int h, int x, int y, int pw, int ph)
{
    const uint8_t fill = cfx_palette_index_map[80];
    const uint8_t fill2 = cfx_palette_index_map[81];
    const uint8_t rim = cfx_palette_index_map[77];
    const uint8_t hi = cfx_palette_index_map[255];
    const uint8_t shadow = cfx_palette_index_map[78];
    for (int yy = y; yy < y + ph; ++yy) {
        if ((unsigned)yy >= (unsigned)h) continue;
        for (int xx = x; xx < x + pw; ++xx) {
            if ((unsigned)xx >= (unsigned)w) continue;
            uint8_t c = (((xx + yy) & 7) == 0) ? fill2 : fill;
            if (xx == x || yy == y) c = hi;
            else if (xx == x + pw - 1 || yy == y + ph - 1) c = shadow;
            else if (xx == x + 1 || yy == y + 1 || xx == x + pw - 2 || yy == y + ph - 2) c = rim;
            framebuffer[yy * w + xx] = c;
        }
    }
}

static void draw_block_rect(CfxRenderer3D *renderer, int x, int y, int s, int type)
{
    const DEFAULT_INT uvmax = (DEFAULT_INT)((CFX_TEXTURE_TILE_SIZE - 1) << 8);
    Point2D p0 = {(DEFAULT_INT)x,       (DEFAULT_INT)y,       0,     0};
    Point2D p1 = {(DEFAULT_INT)(x + s), (DEFAULT_INT)y,       uvmax, 0};
    Point2D p2 = {(DEFAULT_INT)(x + s), (DEFAULT_INT)(y + s), uvmax, uvmax};
    Point2D p3 = {(DEFAULT_INT)x,       (DEFAULT_INT)(y + s), 0,     uvmax};
    cfx_renderer3d_draw_quad(renderer, &p0, &p1, &p2, &p3, type);
}

static void draw_block_piece(CfxRenderer3D *renderer, int base_x, int base_y, const int *cells, int count, int type)
{
    const int s = 20;
    for (int i = 0; i < count; ++i) {
        int cx = cells[i * 2 + 0];
        int cy = cells[i * 2 + 1];
        draw_block_rect(renderer, base_x + cx * s, base_y + cy * s, s - 1, type);
    }
}

static void draw_menu_scene(CfxRenderer3D *renderer, int w, int h)
{
    HeadlessTitleState title_state;
    headless_title_state_init(&title_state);
    for (int i = 0; i < 70; ++i) headless_title_state_step(&title_state);
    clear_to_title(w, h);
    draw_title_cube(renderer, w, h, &title_state);
    draw_text(w, h, 4, 4, "FPS 60", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    draw_text(w, h, (w / 2) - 44, 160, "Select Mode", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    draw_text(w, h, (w / 2) - 52, 180, "> Arcade Mode", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    draw_text(w, h, (w / 2) - 52, 200, "  Training", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    draw_text(w, h, (w / 2) - 52, 220, "  Credits", cfx_palette_index_map[255], cfx_palette_index_map[0]);
}

static void draw_game_scene(CfxRenderer3D *renderer, int w, int h, int variant)
{
    static const int piece_l[] = {0,0, 0,1, 1,1, 2,1};
    static const int piece_t[] = {0,1, 1,1, 2,1, 1,0};
    static const int stack[] = {0,0, 1,0, 2,0, 4,0, 5,0, 6,0, 0,-1, 1,-1, 5,-1};
    clear_to_game(w, h);
    draw_panel(w, h, 4, 4, 76, 40);
    draw_text(w, h, 10, 10, variant ? (variant > 1 ? "2400" : "1200") : "0", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    draw_text(w, h, 10, 28, "FPS 60", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    draw_block_piece(renderer, 104, 166, stack, 9, 3);
    if (variant > 1) {
        draw_block_piece(renderer, 92, 98, piece_l, 4, 2);
        draw_text(w, h, 224, 10, "LEVEL 99", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    } else if (variant) {
        draw_block_piece(renderer, 122, 78, piece_t, 4, 5);
        draw_text(w, h, 224, 10, "LEVEL 42", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    } else {
        draw_block_piece(renderer, 132, 66, piece_l, 4, 1);
        draw_text(w, h, 224, 10, "READY", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    }
}

static void write_showcase_pngs(CfxRenderer3D *renderer, int w, int h, const char *out_dir)
{
    char path[512];
    HeadlessTitleState title_state;
    headless_title_state_init(&title_state);

    for (int i = 0; i < 100; ++i) headless_title_state_step(&title_state);
    clear_to_title(w, h);
    draw_title_cube(renderer, w, h, &title_state);
    draw_text(w, h, 4, 4, "FPS 60", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    draw_text(w, h, (w - 10 * 8) / 2, 204, "PUSH START", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    snprintf(path, sizeof(path), "%s/headless_title_1.png", out_dir);
    cfx_write_png8(path, framebuffer, w, h, palette_rgb);

    for (int i = 0; i < 140; ++i) headless_title_state_step(&title_state);
    clear_to_title(w, h);
    draw_title_cube(renderer, w, h, &title_state);
    draw_text(w, h, 4, 4, "FPS 60", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    draw_text(w, h, (w - 10 * 8) / 2, 204, "PUSH START", cfx_palette_index_map[255], cfx_palette_index_map[0]);
    snprintf(path, sizeof(path), "%s/headless_title_2.png", out_dir);
    cfx_write_png8(path, framebuffer, w, h, palette_rgb);

    draw_menu_scene(renderer, w, h);
    snprintf(path, sizeof(path), "%s/headless_menu.png", out_dir);
    cfx_write_png8(path, framebuffer, w, h, palette_rgb);

    draw_game_scene(renderer, w, h, 0);
    snprintf(path, sizeof(path), "%s/headless_ingame_1.png", out_dir);
    cfx_write_png8(path, framebuffer, w, h, palette_rgb);

    draw_game_scene(renderer, w, h, 1);
    snprintf(path, sizeof(path), "%s/headless_ingame_2.png", out_dir);
    cfx_write_png8(path, framebuffer, w, h, palette_rgb);

    draw_game_scene(renderer, w, h, 2);
    snprintf(path, sizeof(path), "%s/headless_ingame_3.png", out_dir);
    cfx_write_png8(path, framebuffer, w, h, palette_rgb);
}

static int cmp_face(const void *a, const void *b)
{
    const FaceRef *fa = (const FaceRef *)a;
    const FaceRef *fb = (const FaceRef *)b;
    return (fa->z < fb->z) ? -1 : (fa->z > fb->z);
}

static void draw_title_cube(CfxRenderer3D *renderer, int w, int h, const HeadlessTitleState *title)
{
    Point3D transformed_vertices[8];
    Point2D projected_points[8];
    FaceRef faces[6];

    for (DEFAULT_INT i = 0; i < 8; ++i) {
        Point3D v = cube_vertices_template[i];

        v.x = (DEFAULT_INT)(MULTIPLY(v.x, title->pos_z) / DISTANCE_CUBE);
        v.y = (DEFAULT_INT)(MULTIPLY(v.y, title->pos_z) / DISTANCE_CUBE);
        v.z = (DEFAULT_INT)(MULTIPLY(v.z, title->pos_z) / DISTANCE_CUBE);

        v = headless_rotate_x(v, title->angle_x);
        v = headless_rotate_y(v, title->angle_y);
        v.x += title->pos_x;
        v.y += -20;
        v.z += STARTING_Z_OFFSET;

        transformed_vertices[i] = v;
        projected_points[i] = headless_project(v, w, h, 0, 0);
    }

    for (int f = 0; f < 6; ++f) {
        for (int k = 0; k < 4; ++k) {
            faces[f].idx[k] = cube_faces_template[f * 4 + k];
        }

        Point3D *v0 = &transformed_vertices[faces[f].idx[0]];
        Point3D *v1 = &transformed_vertices[faces[f].idx[1]];
        Point3D *v2 = &transformed_vertices[faces[f].idx[2]];

        int32_t ax = v1->x - v0->x;
        int32_t ay = v1->y - v0->y;
        int32_t bx = v2->x - v0->x;
        int32_t by = v2->y - v0->y;
        int32_t nz = MULTIPLY(ax, by) - MULTIPLY(ay, bx);

        faces[f].z = (DEFAULT_INT)(transformed_vertices[faces[f].idx[0]].z +
                                   transformed_vertices[faces[f].idx[1]].z +
                                   transformed_vertices[faces[f].idx[2]].z +
                                   transformed_vertices[faces[f].idx[3]].z);
        faces[f].face_id = f;
        faces[f].tile = 1;
        if (nz > 0) {
            faces[f].tile = -1;
        }
    }

    qsort(faces, 6, sizeof(faces[0]), cmp_face);

    for (int k = 0; k < 6; ++k) {
        int f = faces[k].face_id;
        if (faces[k].tile < 0) {
            continue;
        }

        DEFAULT_INT idx0 = cube_faces_template[f * 4 + 0];
        DEFAULT_INT idx1 = cube_faces_template[f * 4 + 1];
        DEFAULT_INT idx2 = cube_faces_template[f * 4 + 2];
        DEFAULT_INT idx3 = cube_faces_template[f * 4 + 3];
        const DEFAULT_INT *uv = cube_texcoords_template + f * 8;

        Point2D p0 = projected_points[idx0];
        Point2D p1 = projected_points[idx1];
        Point2D p2 = projected_points[idx2];
        Point2D p3 = projected_points[idx3];
        p0.u = uv[0]; p0.v = uv[1];
        p1.u = uv[2]; p1.v = uv[3];
        p2.u = uv[4]; p2.v = uv[5];
        p3.u = uv[6]; p3.v = uv[7];

        cfx_renderer3d_draw_quad(renderer, &p0, &p1, &p2, &p3, 1);
    }
}

int main(int argc, char **argv)
{
    int frames = 120;
    int w = 320, h = 240;
    const char *out_dir = "frames";
    const char *script = NULL;
    int dump_every = 1;
    int showcase = 0;
    enum { CFX_DUMP_PNG, CFX_DUMP_BMP, CFX_DUMP_BOTH } dump_format = CFX_DUMP_PNG;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "--commands") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--dump-every") && i + 1 < argc) dump_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--showcase")) showcase = 1;
        else if (!strcmp(argv[i], "--format") && i + 1 < argc) {
            const char *fmt = argv[++i];
            if (!strcmp(fmt, "bmp")) dump_format = CFX_DUMP_BMP;
            else if (!strcmp(fmt, "both")) dump_format = CFX_DUMP_BOTH;
            else dump_format = CFX_DUMP_PNG;
        }
    }
    if (w != 256 && w != 320) w = 320;
    if (h != 240) h = 240;
    if (dump_every <= 0) dump_every = 1;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", out_dir);
    system(cmd);

    initDivs();
    build_palette_rgb();
    CfxRenderer3D renderer;
    CfxRenderer3DConfig cfg = { framebuffer, (DEFAULT_INT)w, (DEFAULT_INT)h };
    cfx_renderer3d_init(&renderer, &cfg);
    cfx_renderer3d_set_texture_atlas(&renderer, texture, CFX_TEXTURE_TILE_PITCH_BYTES, TEX_TILE_STRIDE);

    if (showcase) {
        write_showcase_pngs(&renderer, w, h, out_dir);
        return 0;
    }

    HeadlessTitleState title_state;
    headless_title_state_init(&title_state);

    for (int f = 0; f < frames; ++f) {
        CfxInputState input;
        cfx_headless_command_for_frame(script, f, &input);
        headless_title_state_step(&title_state);
        clear_to_title(w, h);
        draw_title_cube(&renderer, w, h, &title_state);
        if (title_state.blink_counter < 60) draw_text(w, h, (w - 10 * 6) / 2, 204, "PUSH START", cfx_palette_index_map[0xfb], cfx_palette_index_map[0x1c]);
        draw_text(w, h, 4, 4, "FPS 60", cfx_palette_index_map[0xfb], cfx_palette_index_map[0x1c]);
        if ((f % dump_every) == 0) {
            char path[512];
            if (dump_format == CFX_DUMP_PNG || dump_format == CFX_DUMP_BOTH) {
                snprintf(path, sizeof(path), "%s/frame_%05d.png", out_dir, f);
                cfx_write_png8(path, framebuffer, w, h, palette_rgb);
            }
            if (dump_format == CFX_DUMP_BMP || dump_format == CFX_DUMP_BOTH) {
                snprintf(path, sizeof(path), "%s/frame_%05d.bmp", out_dir, f);
                cfx_write_bmp8(path, framebuffer, w, h, palette_rgb);
            }
        }
    }
    return 0;
}
