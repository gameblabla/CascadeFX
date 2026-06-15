
// Global pointer to the current framebuffer position
static int16_t *fb_ptr;

// Function to write a 16-bit word to the framebuffer and advance the pointer
static inline void SetPixel16(int32_t color) {
    *fb_ptr = (int16_t)color;
}


// Helper function to fetch the texture color
static inline uint32_t fetchTextureColor(int32_t u, int32_t v, int32_t du, int32_t dv) {
    // Convert fixed-point texture coordinates to integer in [0..31]
    int32_t tex_u1 = (u >> 8) & 31;
    int32_t tex_v1 = (v >> 8) & 31;

    int32_t tex_u2 = ((u + du) >> 8) & 31;
    int32_t tex_v2 = ((v + dv) >> 8) & 31;

    // Reinterpret texture as a uint8_t array for direct pixel access
    uint8_t* texture8 = (uint8_t*)texture_actual;

    // Calculate the 1D indices for each texel
    uint32_t texture_index1 = tex_v1 * 32 + tex_u1;
    uint32_t texture_index2 = tex_v2 * 32 + tex_u2;

    // Fetch the two colors directly from the reinterpreted texture
    uint8_t color1 = texture8[texture_index1];
    uint8_t color2 = texture8[texture_index2];

    // Pack the two 8-bit colors into a single 16-bit value
    return (uint16_t)((color1 << 8) | color2);
}

// Updated drawScanline function
static inline void drawScanline(int32_t xs, int32_t xe, int32_t u, int32_t v,
    int32_t du, int32_t dv, int y) {
    // Set up the framebuffer pointer at the start of this scanline
    fb_ptr = (int16_t *)GAME_FRAMEBUFFER + y * (SCREEN_WIDTH / 2) + (xs / 2);
    
    // How many 16-bit writes (pairs of pixels) to process
	int32_t num_pairs = (xe - xs + 1) / 2;

    while (num_pairs > 0)
    {
        SetPixel16(fetchTextureColor(u, v, du, dv));
        
        u += du * 2;
        v += dv * 2;
        fb_ptr++;

        num_pairs--;
    }
}
