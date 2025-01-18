
static inline uint32_t fetchTextureColor(int32_t u, int32_t v) {
    // Convert fixed-point texture coordinates to integer
    uint8_t tex_u = (u >> 8) & 31;  // u is in 8.8 fixed-point format, mask to 0-31
    uint8_t tex_v = (v >> 8) & 31;  // v is in 8.8 fixed-point format, mask to 0-31

    // Calculate the index into the texture array
    uint32_t texture_index = tex_v * 32 + tex_u;  // Texture is 32x32

    // Fetch the color from the texture
    uint8_t color_index = texture[texture_index];  // texture is uint8_t array

    return color_index;  // Return the 8-bit color index
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

static inline void drawScanline(int32_t xs, int32_t xe, DEFAULT_INT u, DEFAULT_INT v,
    DEFAULT_INT du, DEFAULT_INT dv, DEFAULT_INT y) {
#if defined(_8BITS_WRITES)
    for (int32_t x = xs; x <= xe; x++) {
        DEFAULT_INT color = fetchTextureColor(u, v);
        SetPixel8(x, y, color);
        u += du;
        v += dv;
    }
#elif defined(_16BITS_WRITES)

#ifndef FAST_DRAWING
    if (xs & 1) {
        SetPixel8(xs, y, fetchTextureColor(u, v));
        xs++;
        u += du;
        v += dv;
    }
#endif

    int32_t x;
    for (x = xs; x <= xe - 1; x += 2) {
        DEFAULT_INT color1 = fetchTextureColor(u, v);
        u += du;
        v += dv;
        DEFAULT_INT color2 = fetchTextureColor(u, v);
        u += du;
        v += dv;
#ifdef BIGENDIAN_TEXTURING
        DEFAULT_INT colors = (color1 << 8) | color2;
#else
        DEFAULT_INT colors = (color2 << 8) | color1;
#endif
        SetPixel16(x, y, colors);
    }
    
#ifndef FAST_DRAWING
    if (x == xe) {
        int32_t color = fetchTextureColor(u, v);
        SetPixel8(x, y, color);
    }
#endif
    
#endif
}