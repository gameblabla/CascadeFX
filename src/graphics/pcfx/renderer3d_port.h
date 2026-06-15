#ifndef CFX_RENDERER3D_PORT_H
#define CFX_RENDERER3D_PORT_H
/* Keep the PC-FX foreground on the direct-row/single-LUT path, but bypass the
   64 KiB LUT rebuilds for offscreen locked-board rebuilds by sampling the
   source tile directly on non-axis faces. */
#define CFX_RENDERER_MULTI_LUT 0
#define CFX_RENDERER_WORD_ORDER_PCFX 1
#define CFX_RENDERER_HEADLESS_BYTES 0
#if defined(__v810__)
#define CFX_RENDERER_USE_V810_ASM 1
#else
#define CFX_RENDERER_USE_V810_ASM 0
#endif
#define CFX_RENDERER_DIRECT_RECT 1
#define CFX_RENDERER_DIRECT_ROW_LUT 1
#define CFX_RENDERER_DIRECT_GENERIC_TILE 0
#define CFX_RENDERER_DIRECT_KRAM 1
#define CFX_RENDERER_QUAD_SCANLINE 1
#define CFX_RENDERER_USE_SH1_ASM 0
#endif
