
// Helper function to fetch the texture color
static inline uint32_t fetchTextureColor(int32_t u, int32_t v, int tetromino_type) {
    uint8_t tex_u = ((u >> 8) & 31);
    uint8_t tex_v_full = ((v >> 8) & 31);
    bool is_brighter = tex_v_full >= 16;
    uint8_t local_v = tex_v_full & 0x0F;
    int tile_index = tetromino_type * 2 + (is_brighter ? 1 : 0);
    //if (tile_index >= 14) tile_index = 0;
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

// Updated drawScanline function
static inline void drawScanline(int32_t xs, int32_t xe, int32_t u, int32_t v,
    int32_t du, int32_t dv, int y, int tetromino_type) {
#if defined(_8BITS_WRITES)
    uint8_t *fb_ptr = ((uint8_t*)GAME_FRAMEBUFFER) + y * SCREEN_WIDTH + xs;
    int32_t num_pixels = xe - xs + 1;
    for (int32_t i = 0; i < num_pixels; i++) {
        int32_t color = fetchTextureColor(u, v, tetromino_type);
        *fb_ptr++ = (uint8_t)color;
        u += du;
        v += dv;
    }
#elif defined(_16BITS_WRITES)
    uint8_t *fb_line_start = ((uint8_t*)GAME_FRAMEBUFFER) + y * SCREEN_WIDTH;
    uint8_t *fb_ptr8 = fb_line_start + xs;
    int32_t x = xs;

    // Handle odd starting pixel
    /*if (x & 1) {
        int32_t color = fetchTextureColor(u, v, tetromino_type);
        *fb_ptr8++ = (uint8_t)color;
        x++;
        u += du;
        v += dv;
    }*/

    // Now x is even; proceed with 16-bit writes
    uint16_t *fb_ptr16 = (uint16_t*)fb_ptr8;
    int32_t num_pixels = xe - x + 1;
    int32_t num_pairs = num_pixels / 2;
    for (int32_t i = 0; i < num_pairs; i++) {
        int32_t color1 = fetchTextureColor(u, v, tetromino_type);
        u += du;
        v += dv;
        int32_t color2 = fetchTextureColor(u, v, tetromino_type);
        u += du;
        v += dv;
    #ifdef BIGENDIAN_TEXTURING
        uint16_t colors = (uint16_t)((color1 << 8) | color2);
    #else
        uint16_t colors = (uint16_t)((color2 << 8) | color1);
    #endif
        *fb_ptr16++ = colors;
    }

    // Handle remaining pixel if any
    /*if (num_pixels & 1) {
        int32_t color = fetchTextureColor(u, v, tetromino_type);
        u += du;
        v += dv;
        fb_ptr8 = (uint8_t*)fb_ptr16;
        *fb_ptr8 = (uint8_t)color;
    }*/
#endif
}
