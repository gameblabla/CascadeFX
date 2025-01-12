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
static inline uint16_t fetchTextureColor16(register uint16_t *texture16, register int32_t index) {
    return texture16[index];
}

//-----------------------------------------------------------------------------
// 2) The drawScanline function with refactored logic
//-----------------------------------------------------------------------------
static inline void drawScanline(int32_t xs, int32_t xe,
								DEFAULT_INT u,
								DEFAULT_INT v,
								DEFAULT_INT du,
								DEFAULT_INT dv,
                                DEFAULT_INT y,
                                DEFAULT_INT tetromino_type)
{
	// Pre-calculate as much as possible outside the inner loop
	uint16_t *texture16 = (uint16_t *)texture;
   
	DEFAULT_INT tile0 = tetromino_type * 2;      // darker tile
	DEFAULT_INT tile1 = tile0 + 1;               // brighter tile

	DEFAULT_INT baseIndex0 = tile0 * (32 * 16) / 2;  // tile0 * 256
	DEFAULT_INT baseIndex1 = tile1 * (32 * 16) / 2;  // tile1 * 256
    
    // Set up the framebuffer pointer at the start of this scanline
    fb_ptr = (int16_t *)GAME_FRAMEBUFFER + y * (SCREEN_WIDTH / 2) + (xs / 2);
    
    // How many 16-bit writes (pairs of pixels) to process
	int32_t num_pairs = (xe - xs + 1) / 2;

    while (num_pairs > 0)
    {
        // Compute texture coordinates
         DEFAULT_INT tex_u = (int16_t)((u >> 8) & 31);
         DEFAULT_INT tex_v_full = (int16_t)((v >> 8) & 31);
        
        // Determine which tile to use (darker vs brighter) and compute the local row within that tile
         DEFAULT_INT baseIndex = (tex_v_full >= 16) ? baseIndex1 : baseIndex0;
         DEFAULT_INT local_v   = (int16_t)(tex_v_full & 0x0F);

        // Compute final index in the texture array ( /2 because 2 bytes per 16-bit)
        // offset = local_v*32 + tex_u, then we divide by 2 for the 16-bit array
         DEFAULT_INT offset = (local_v * 32 + tex_u) >> 1;

        // Grab the color from the texture
		uint16_t color = fetchTextureColor16(texture16, baseIndex + offset);


        // Write color to framebuffer
        SetPixel16(color);

        // Go to the next 16-bit cell in the framebuffer
        fb_ptr++;

        u += du;
        u += du;
        v += dv;
        v += dv;

        num_pairs--;
    }
}
