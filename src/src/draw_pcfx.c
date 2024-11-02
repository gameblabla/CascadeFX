static inline int16_t fetchTextureColor(int32_t u, int32_t v, int tetromino_type) {
    int tex_u = ((u >> 8) & 31);
    int tex_v_full = ((v >> 8) & 31);
    int local_v = tex_v_full & 0x0F;
    bool is_brighter = tex_v_full >= 16;
    int tile_index = tetromino_type * 2 + (is_brighter ? 1 : 0);
    //if (tile_index >= 14) tile_index = 0;
    
    // Calculate the index in the texture array
    int index = tile_index * 32 * 16 + local_v * 32 + tex_u;
    
    // Since texture is stored as uint16_t but contains 8-bit values, we access it as uint16_t*
    int8_t *texture16 = (int8_t *)texture;
    
    // Extract the 8-bit value from the 16-bit storage
    int16_t color = (int16_t)(texture16[index] ); // Assuming lower byte contains the color index
    
    return color;
}

static inline int16_t fetchTextureColor16(int32_t u, int32_t v, int tetromino_type) {
    int32_t tex_u = ((u >> 8) & 31);
    int32_t tex_v_full = ((v >> 8) & 31);
    bool is_brighter = tex_v_full >= 16;
    int32_t local_v = tex_v_full & 0x0F;
    int tile_index = tetromino_type * 2 + (is_brighter ? 1 : 0);
	//if (tile_index >= 14) tile_index = 0;

    // Calculate the index in the texture array
    int index = (tile_index * 32 * 16 + local_v * 32 + tex_u)/2;

	int16_t *texture16 = (int16_t *)texture;
    int16_t color = (int16_t)(texture16[index] ); // Assuming lower byte contains the color index
    
    return color;
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

    // Initialize framebuffer pointer to the starting position
    fb_ptr = (int16_t *)framebuffer_game + y * (SCREEN_WIDTH / 2) + (xs / 2);

    // Handle the first pixel if xs is odd
    if (xs & 1) {
        int32_t color = fetchTextureColor(u, v, tetromino_type);
        u += du;
        v += dv;

        // Modify the lower byte of the current 16-bit word
        SetPixel8_xs(color);

        // Move to the next 16-bit word
        fb_ptr++;
        xs++;
    }

    // Calculate the number of full 16-bit words to write
    int32_t num_pairs = (xe - xs + 1) / 2;
    
    // Use a decrementing loop for processing pixel pairs
    while(num_pairs)
    {
        int16_t colors = fetchTextureColor16(u, v, tetromino_type);
        u += du;
        v += dv;
        u += du;
        v += dv;
        
        // Set two pixels at once and advance the pointer
        SetPixel16(colors);
        fb_ptr++;
        num_pairs--;
    }

    // Handle the last pixel if xe is odd
    if (xs == xe) {
        int32_t color = fetchTextureColor(u, v, tetromino_type);

        // Modify the upper byte of the current 16-bit word
        SetPixel8_xe(color);
    }
}
