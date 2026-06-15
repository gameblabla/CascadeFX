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


//-----------------------------------------------------------------------------
// 1) A simpler fetch function that only does a texture16[index] lookup
//-----------------------------------------------------------------------------
static inline uint16_t fetchTextureColor16(DEFAULT_INT u, DEFAULT_INT v, DEFAULT_INT du, DEFAULT_INT dv) {
#ifdef FAST_DRAWING
	// This cheats quite a bit... But the end result is that it's much faster than the code below.
	// Effectively, it's twice as fast but the end result is that it looks worse...
	// The textures could be made to look somewhat better if modified accordingly.
    uint16_t tex_u1 = (u >> 8) & 31;
    uint16_t tex_v1 = (v >> 8) & 31;
    uint16_t texture_index1 = tex_v1 * 32 + tex_u1;
    uint16_t word1 = texture_actual[texture_index1 >> 1];
    return word1;
#else
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

#endif
}

//-----------------------------------------------------------------------------
// 2) The drawScanline function with refactored logic
//-----------------------------------------------------------------------------
static inline void drawScanline(int32_t xs, int32_t xe,
								DEFAULT_INT u,
								DEFAULT_INT v,
								DEFAULT_INT du,
								DEFAULT_INT dv,
                                DEFAULT_INT y)
{

    
    // Set up the framebuffer pointer at the start of this scanline
    fb_ptr = (int16_t *)GAME_FRAMEBUFFER + y * (SCREEN_WIDTH / 2) + (xs / 2);
    
    // How many 16-bit writes (pairs of pixels) to process
	int32_t num_pairs = (xe - xs + 1) / 2;

    while (num_pairs > 0)
    {
        SetPixel16(fetchTextureColor16(u, v, du, dv));
        
        u += du * 2;
        v += dv * 2;
        fb_ptr++;

        num_pairs--;
    }
}
