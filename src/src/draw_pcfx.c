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

static inline uint8_t getTexU(int32_t uv) {
    // Low 16 bits = U in 8.8 => integer part is the top 8 bits
    // e.g. 0xUUFF => (0xUUFF >> 8) = 0xUU => & 31 for 32 wide
    return (uint8_t)((uv >> 8) & 0x1F);
}

static inline uint8_t getTexV(uint32_t uv) {
    return (uint8_t)((uv >> 24) & 0x1F);
}



static inline uint8_t fetchTextureColor_uv(int32_t uv)
{
    // Extract integer U and V
    uint8_t tex_u = getTexU(uv);
    uint8_t tex_v = getTexV(uv);

    // Lookup 8-bit color in a 32x32 texture
    return texture_actual[tex_v * 32 + tex_u];
}


static inline uint16_t fetchTextureColor16_uv(int32_t uv, int32_t duv)
{
    // First pixel
    uint8_t c1 = fetchTextureColor_uv(uv);

    // Second pixel at uv + duv
    uint8_t c2 = fetchTextureColor_uv(uv + duv);

#ifdef BIGENDIAN_TEXTURING
    return (uint16_t)((c1 << 8) | c2);
#else
    return (uint16_t)((c2 << 8) | c1);
#endif
}



static inline void drawScanline(int32_t xs, int32_t xe, int32_t u, int32_t v,
    int32_t du, int32_t dv, int y) {

    int32_t uv = ((int32_t)((uint16_t)v) << 16) |
                  ((int32_t)((uint16_t)u));

    int32_t duv = ((int32_t)((uint16_t)dv) << 16) |
                   ((int32_t)((uint16_t)du));

    fb_ptr = (int16_t *)framebuffer_game
             + y * (SCREEN_WIDTH / 2)
             + (xs / 2);

    if (xs & 1) {
        uint8_t color = fetchTextureColor_uv(uv);
        uv += duv;
        SetPixel8_xs(color);
        fb_ptr++;
        xs++;
    }


    int32_t width = xe - xs ; 
    int32_t num_pairs = width / 2;

     do {
        uint16_t colors = fetchTextureColor16_uv(uv, duv);
        uv += duv;
        uv += duv;
        SetPixel16(colors);
        fb_ptr++;
    } while (num_pairs--);


    if (xs == xe) 
    {
        uint8_t color = fetchTextureColor_uv(uv);
        SetPixel8_xe(color);
    }
}
