static inline uint32_t fetchTextureColor(int32_t u, int32_t v) {
    int tex_u = ((u >> 8) & 31);
    int tex_v = ((v >> 8) & 31);
    
    // Calculate the index in the texture array
    uint32_t texture_index = tex_v * 32 + tex_u;  // Texture is 32x32
    
    // Since texture is stored as uint16_t but contains 8-bit values, we access it as uint16_t*
    int8_t *texture16 = (int8_t *)texture_actual;
    
    // Extract the 8-bit value from the 16-bit storage
    int16_t color = (int16_t)(texture16[texture_index] ); // Assuming lower byte contains the color index
    
    return color;
}

static inline uint16_t fetchTextureColor16(DEFAULT_INT u, DEFAULT_INT v, DEFAULT_INT du, DEFAULT_INT dv) {
	
    // Convert fixed-point texture coordinates to integer
    uint8_t tex_u1 = (u >> 8) & 31;  // First pixel's u coordinate
    uint8_t tex_v1 = (v >> 8) & 31;  // First pixel's v coordinate

    // Calculate the second pixel's texture coordinates
    uint8_t tex_u2 = ((u + du) >> 8) & 31;  // Second pixel's u coordinate
    uint8_t tex_v2 = ((v + dv) >> 8) & 31;  // Second pixel's v coordinate

    // Calculate the indices into the texture array
    uint32_t texture_index1 = tex_v1 * 32 + tex_u1;  // First pixel's index
    uint32_t texture_index2 = tex_v2 * 32 + tex_u2;  // Second pixel's index

    // Fetch the two 8-bit colors from the texture
    uint8_t color1 = texture_actual[texture_index1];  // First pixel's color
    uint8_t color2 = texture_actual[texture_index2];  // Second pixel's color

    // Pack the two 8-bit colors into a 16-bit value
#ifdef BIGENDIAN_TEXTURING
    return (color1 << 8) | color2;  // Big-endian: color1 in MSB, color2 in LSB
#else
    return (color2 << 8) | color1;  // Little-endian: color2 in MSB, color1 in LSB
#endif
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
    int32_t du, int32_t dv, int y) {

    // Initialize framebuffer pointer to the starting position
    fb_ptr = (int16_t *)framebuffer_game + y * (SCREEN_WIDTH / 2) + (xs / 2);

    // Handle the first pixel if xs is odd
    if (xs & 1) {
        int32_t color = fetchTextureColor(u, v);
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
        int16_t colors = fetchTextureColor16(u, v, du, dv);
        u += du * 2;
        v += dv * 2;
        
        // Set two pixels at once and advance the pointer
        SetPixel16(colors);
        fb_ptr++;
        num_pairs--;
    }

    // Handle the last pixel if xe is odd
    if (xs == xe) {
        int32_t color = fetchTextureColor(u, v);

        // Modify the upper byte of the current 16-bit word
        SetPixel8_xe(color);
    }
}
