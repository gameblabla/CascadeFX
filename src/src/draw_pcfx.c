static inline int16_t fetchTextureColor(int32_t u, int32_t v, int tetromino_type) {
    int tex_u = ((u >> 8) & 31);
    int tex_v_full = ((v >> 8) & 31);
    int local_v = tex_v_full & 0x0F;
    bool is_brighter = tex_v_full >= 16;
    int tile_index = tetromino_type * 2 + (is_brighter ? 1 : 0);
    //if (tile_index >= 14) tile_index = 0;
    
    // Calculate the index in the texture array
    int index = tile_index * 32 * 16 + local_v * 32 + tex_u;
    
    /* The PC-FX asset is emitted in PNG byte order. Signed bytes intentionally
       use the V810's sign-extending LD.B; the framebuffer writers consume the
       texel's low eight bits. */
    return texture[index];
}

// Global pointer to the current framebuffer position
static int16_t *fb_ptr;

// Function to read a 16-bit word from the framebuffer
static inline int16_t GetPixel16() {
    return *fb_ptr;
}

// Function to write a 16-bit word to the framebuffer and advance the pointer
static inline void SetPixel16(int32_t color) {
    *fb_ptr = (int16_t)color;
}

// Function to set an 8-bit pixel in the lower byte without advancing the pointer
static inline void SetPixel8_xs(int32_t color) {
    *fb_ptr = (*fb_ptr & 0xFF00) | (color & 0x00FF);
}

// Function to set an 8-bit pixel in the upper byte without advancing the pointer
static inline void SetPixel8_xe(int32_t color) {
    *fb_ptr = (*fb_ptr & 0x00FF) | ((color & 0x00FF) << 8);
}


// Function to draw a scanline using pointer arithmetic
static inline void drawScanline(int32_t xs, int32_t xe, int32_t u, int32_t v,
    int32_t du, int32_t dv, int y, int tetromino_type) {

    if (y < 0 || y >= SCREEN_HEIGHT || xs > xe)
        return;
    if (xs < 0) {
        int32_t skip = -xs;
        u += du * skip;
        v += dv * skip;
        xs = 0;
    }
    if (xe >= SCREEN_WIDTH)
        xe = SCREEN_WIDTH - 1;
    if (xs > xe)
        return;

    int32_t x = xs;
    fb_ptr = (int16_t *)framebuffer_game + y * (SCREEN_WIDTH / 2) + (x / 2);

    if (x & 1) {
        /* KING's 8bpp word stores the left (even-x) pixel in the high byte. */
        SetPixel8_xs(fetchTextureColor(u, v, tetromino_type));
        fb_ptr++;
        x++;
        u += du;
        v += dv;
    }

    while (x + 1 <= xe) {
        /* Keep the signed sample sign-extended. Shifting it into the high byte
           lets uint16_t narrowing discard the rest; the low-byte sample must
           be narrowed explicitly before packing. */
        uint32_t first = (uint32_t)(int32_t)fetchTextureColor(u, v, tetromino_type);
        uint32_t second = (uint8_t)fetchTextureColor(u + du, v + dv, tetromino_type);
        uint16_t packed = (uint16_t)((first << 8) | second);
        SetPixel16(packed);
        fb_ptr++;
        x += 2;
        u += du * 2;
        v += dv * 2;
    }

    if (x <= xe)
        SetPixel8_xe(fetchTextureColor(u, v, tetromino_type));
}
