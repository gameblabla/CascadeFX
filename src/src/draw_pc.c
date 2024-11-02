
// Helper function to fetch the texture color
static inline uint32_t fetchTextureColor(int32_t u, int32_t v, int tetromino_type) {
    uint8_t tex_u = ((u >> 8) & 31);
    uint8_t tex_v_full = ((v >> 8) & 31);
    bool is_brighter = tex_v_full >= 16;
    uint8_t local_v = tex_v_full & 0x0F;
    int tile_index = tetromino_type * 2 + (is_brighter ? 1 : 0);
    if (tile_index >= 14) tile_index = 0;
#if defined(_16BITS_WRITES)
    // Since texture is stored as uint16_t but contains 8-bit values, we access it as uint16_t*
    uint8_t *texture16 = (uint8_t *)texture;
    
    // Extract the 8-bit value from the 16-bit storage
    uint32_t color = (uint8_t)(texture16[tile_index * 32 * 16 + local_v * 32 + tex_u] ); // Assuming lower byte contains the color index
    return color;
#else
    return texture[tile_index * 32 * 16 + local_v * 32 + tex_u];
#endif
}


// Pixel setting functions
static inline void SetPixel8(uint32_t x, uint32_t y, int32_t color) {
	//if (y >= SCREEN_HEIGHT-1 || x >= SCREEN_WIDTH-1) return; 
    ((uint8_t*)GAME_FRAMEBUFFER)[y * SCREEN_WIDTH + x] = color;
}

static inline void SetPixel16(uint32_t x, uint32_t y, int32_t color) {
	//if (y >= SCREEN_HEIGHT-1 || x >= SCREEN_WIDTH-1) return; 
    int32_t index = y * SCREEN_WIDTH + x;
    ((uint16_t*)GAME_FRAMEBUFFER)[index >> 1] = color;
}

static inline void drawScanline(int32_t xs, int32_t xe, int32_t u, int32_t v,
    int32_t du, int32_t dv, int y, int tetromino_type) {
#if defined(_8BITS_WRITES)
    for (int32_t x = xs; x <= xe; x++) {
        int32_t color = fetchTextureColor(u, v, tetromino_type);
        SetPixel8(x, y, color);
        u += du;
        v += dv;
    }
#elif defined(_16BITS_WRITES)
    if (xs & 1) {
        SetPixel8(xs, y, fetchTextureColor(u, v, tetromino_type));
        xs++;
        u += du;
        v += dv;
    }

    int32_t x;
    for (x = xs; x <= xe - 1; x += 2) {
        int32_t color1 = fetchTextureColor(u, v, tetromino_type);
        u += du;
        v += dv;
        int32_t color2 = fetchTextureColor(u, v, tetromino_type);
        u += du;
        v += dv;
#ifdef BIGENDIAN_TEXTURING
        int32_t colors = (color1 << 8) | color2;
#else
        int32_t colors = (color2 << 8) | color1;
#endif
        SetPixel16(x, y, colors);
    }
    if (x == xe) {
        int32_t color = fetchTextureColor(u, v, tetromino_type);
        SetPixel8(x, y, color);
    }
#endif
}