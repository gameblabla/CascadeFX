.global _king_kram_write_buffer
.global _king_kram_write_buffer_bytes
.global _king_kram_fill_words
.global _king_kram_clear_rect_256_packed
.global _king_kram_clear_rect_256_zero_packed
.global _king_kram_upload_rect_256_packed
.global _eris_king_set_bg0_affine_coefficient_a
.global _eris_king_set_bg0_affine_coefficient_b
.global _eris_king_set_bg0_affine_coefficient_c
.global _eris_king_set_bg0_affine_coefficient_d

.global _eris_king_set_bg_affine_center_x
.global _eris_king_set_bg_affine_center_y

.macro	set_rrg	reg
	out.h	\reg, 0x600[r0]
.endm

.macro	set_reg reg, tmp
	movea	\reg, r0, \tmp
	set_rrg	\tmp
.endm

_eris_king_set_bg0_affine_coefficient_a:
    set_reg	0x38, r10
    out.h	r6, 0x604[r0]
    jmp     [lp]

_eris_king_set_bg0_affine_coefficient_b:
    set_reg	0x39, r10
    out.h	r6, 0x604[r0]
    jmp     [lp]

_eris_king_set_bg0_affine_coefficient_c:
    set_reg	0x3A, r10
    out.h	r6, 0x604[r0]
    jmp     [lp]

_eris_king_set_bg0_affine_coefficient_d:
    set_reg	0x3B, r10
    out.h	r6, 0x604[r0]
    jmp     [lp]

_eris_king_set_bg_affine_center_x:
    set_reg	0x3C, r10
    out.h	r6, 0x604[r0]
    jmp     [lp]
    
_eris_king_set_bg_affine_center_y:
    set_reg	0x3D, r10
    out.h	r6, 0x604[r0]
    jmp     [lp]
_king_kram_fill_words:
	set_reg	0xE, r10
	cmp	r0, r7
	be	3f
	mov	r7, r8
	shr	3, r8
	cmp	r0, r8
	be	2f
1:
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	addi	-1, r8, r8
	bne	1b
2:
	andi	7, r7, r7
	cmp	r0, r7
	be	3f
4:
	out.h	r6, 0x604[r0]
	addi	-1, r7, r7
	bne	4b
3:
	jmp	[lp]


_king_kram_write_buffer:
	set_reg	0xE, r10
	add r6,r7
1:
	ld.w	0[r6],r8
	out.h	r8, 0x604[r0]
	shr 16, r8
	out.h	r8, 0x604[r0]

	ld.w	4[r6],r8
	out.h	r8, 0x604[r0]
	shr 16, r8
	out.h	r8, 0x604[r0]

	ld.w	8[r6],r8
	out.h	r8, 0x604[r0]
	shr 16, r8
	out.h	r8, 0x604[r0]

	ld.w	12[r6],r8
	out.h	r8, 0x604[r0]
	shr 16, r8
	out.h	r8, 0x604[r0]

	addi 16,r6,r6

	cmp	r7,r6
	bl	1b
	jmp	[lp]


_king_kram_write_buffer_bytes:
	set_reg	0xE, r10
	add r6,r7
1:
	ld.w	0[r6],r11
	mov r11,r13
	shr 8,r13
	mov r11,r15
	andi 255,r13,r14
	shl 8,r15
	andi 65280,r13,r13
	shr 24,r11
	or r15,r14
	or r11,r13
	out.h	r14, 0x604[r0]
	out.h	r13, 0x604[r0]

	ld.w	4[r6],r11
	mov r11,r13
	shr 8,r13
	mov r11,r15
	andi 255,r13,r14
	shl 8,r15
	andi 65280,r13,r13
	shr 24,r11
	or r15,r14
	or r11,r13
	out.h	r14, 0x604[r0]
	out.h	r13, 0x604[r0]

	ld.w	8[r6],r11
	mov r11,r13
	shr 8,r13
	mov r11,r15
	andi 255,r13,r14
	shl 8,r15
	andi 65280,r13,r13
	shr 24,r11
	or r15,r14
	or r11,r13
	out.h	r14, 0x604[r0]
	out.h	r13, 0x604[r0]

	ld.w	12[r6],r11
	mov r11,r13
	shr 8,r13
	mov r11,r15
	andi 255,r13,r14
	shl 8,r15
	andi 65280,r13,r13
	shr 24,r11
	or r15,r14
	or r11,r13
	out.h	r14, 0x604[r0]
	out.h	r13, 0x604[r0]

	addi 16,r6,r6

	cmp	r7,r6
	bl	1b
	jmp	[lp]

/* Fill a rectangle in linear 256-pixel KING KRAM pages.
   Args: r6=value, r7=page_word_offset,
         r8=(x0 | y0<<16), r9=(width_words | rows<<16).
   Packed arguments keep this under the V810-GCC four-register ABI and avoid
   stack argument loads in this hot path. */
_king_kram_clear_rect_256_packed:
	mov	r9, r10
	andi	65535, r10, r10        /* width_words */
	mov	r9, r11
	shr	16, r11                 /* rows */
	cmp	r0, r10
	be	9f
	cmp	r0, r11
	be	9f
	mov	r8, r12
	shr	16, r12                 /* y0 */
	shl	7, r12                  /* y0 * (256/2) words */
	mov	r8, r13
	andi	65535, r13, r13        /* x0 */
	shr	1, r13                  /* x0 / 2 words */
	add	r13, r12
	add	r7, r12                  /* current KING word address */
	movea	1, r0, r13
	shl	18, r13                 /* KING increment flag */
	movea	128, r0, r14           /* words per 256-pixel row */
1:
	mov	r12, r15
	or	r13, r15
	movea	13, r0, r7
	out.h	r7, 0x600[r0]
	out.w	r15, 0x604[r0]
	movea	14, r0, r7
	out.h	r7, 0x600[r0]
	mov	r10, r8
	shr	3, r8
	cmp	r0, r8
	be	3f
2:
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	out.h	r6, 0x604[r0]
	addi	-1, r8, r8
	bne	2b
3:
	mov	r10, r9
	andi	7, r9, r9
	cmp	r0, r9
	be	5f
4:
	out.h	r6, 0x604[r0]
	addi	-1, r9, r9
	bne	4b
5:
	add	r14, r12
	addi	-1, r11, r11
	bne	1b
9:
	jmp	[lp]

/* Zero-fill a rectangle in linear 256-pixel KING KRAM pages.
   Args: r6=page_word_offset, r7=(x0 | y0<<16), r8=(width_words | rows<<16).
   PC-FX call sites pass 16-pixel-aligned X and width, so width_words is a
   multiple of eight.  This removes the dead nonzero fill path and the residual
   loop from the clear hot path; full-width clears are streamed linearly. */
_king_kram_clear_rect_256_zero_packed:
	mov	r8, r10
	andi	65535, r10, r10        /* width_words */
	mov	r8, r11
	shr	16, r11                 /* rows */
	cmp	r0, r10
	be	9f
	cmp	r0, r11
	be	9f
	mov	r7, r12
	shr	16, r12                 /* y0 */
	shl	7, r12                  /* y0 * 128 words */
	mov	r7, r13
	andi	65535, r13, r13        /* x0 */
	shr	1, r13                  /* x0 / 2 words */
	add	r13, r12
	add	r6, r12                  /* current KING word address */
	movea	1, r0, r13
	shl	18, r13                 /* KING increment flag */
	movea	128, r0, r14          /* words per 256-pixel row */
	cmp	r14, r10
	bne	1f

	/* Whole rows: one KING seek, one data-register select, then a single
	   contiguous zero stream. */
	mov	r12, r15
	or	r13, r15
	movea	13, r0, r7
	out.h	r7, 0x600[r0]
	out.w	r15, 0x604[r0]
	movea	14, r0, r7
	out.h	r7, 0x600[r0]
	shl	7, r11                  /* rows * 128 words */
	mov	r11, r8
	shr	5, r8                   /* 32-word blocks */
	cmp	r0, r8
	be	6f
7:
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	addi	-1, r8, r8
	bne	7b
6:
	andi	31, r11, r11
	cmp	r0, r11
	be	9f
8:
	out.h	r0, 0x604[r0]
	addi	-1, r11, r11
	bne	8b
	br	9f

1:
	/* Partial rows: one seek per output row.  Width is 16-pixel aligned, so
	   width_words is already a multiple of eight. */
	mov	r12, r15
	or	r13, r15
	movea	13, r0, r7
	out.h	r7, 0x600[r0]
	out.w	r15, 0x604[r0]
	movea	14, r0, r7
	out.h	r7, 0x600[r0]
	mov	r10, r8
	shr	3, r8
2:
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	out.h	r0, 0x604[r0]
	addi	-1, r8, r8
	bne	2b
	add	r14, r12
	addi	-1, r11, r11
	bne	1b
9:
	jmp	[lp]

/* Upload a rectangle from a 256-byte-pitch 8bpp CPU framebuffer to KING KRAM.
   Args: r6=src base, r7=page_word_offset,
         r8=(x0 | y0<<16), r9=(row_bytes | rows<<16).
   Preconditions used by callers: x0 and row_bytes are 16-byte aligned. */
_king_kram_upload_rect_256_packed:
	mov	r9, r10
	andi	65535, r10, r10        /* row_bytes */
	mov	r9, r11
	shr	16, r11                 /* rows */
	cmp	r0, r10
	be	9f
	cmp	r0, r11
	be	9f
	mov	r8, r12
	shr	16, r12                 /* y0 */
	shl	8, r12                  /* y0 * 256 bytes */
	mov	r8, r8
	andi	65535, r8, r8          /* x0 */
	add	r8, r12                  /* byte offset */
	add	r12, r6                  /* current source row */
	shr	1, r12                  /* word offset in KING page */
	add	r7, r12                  /* current KING word address */
	movea	1, r0, r9
	shl	18, r9                  /* KING increment flag */
	movea	128, r0, r14           /* words per 256-pixel row */
	movea	256, r0, r13
	sub	r10, r13                 /* source delta from row end to next row */
1:
	mov	r12, r15
	or	r9, r15
	movea	13, r0, r7
	out.h	r7, 0x600[r0]
	out.w	r15, 0x604[r0]
	movea	14, r0, r7
	out.h	r7, 0x600[r0]
	mov	r6, r15
	add	r10, r15                 /* row end */
2:
	ld.w	0[r6], r8
	out.h	r8, 0x604[r0]
	shr	16, r8
	out.h	r8, 0x604[r0]

	ld.w	4[r6], r8
	out.h	r8, 0x604[r0]
	shr	16, r8
	out.h	r8, 0x604[r0]

	ld.w	8[r6], r8
	out.h	r8, 0x604[r0]
	shr	16, r8
	out.h	r8, 0x604[r0]

	ld.w	12[r6], r8
	out.h	r8, 0x604[r0]
	shr	16, r8
	out.h	r8, 0x604[r0]

	addi	16, r6, r6
	cmp	r15, r6
	bl	2b
	add	r13, r6                  /* next source row */
	add	r14, r12                 /* next KING row */
	addi	-1, r11, r11
	bne	1b
9:
	jmp	[lp]

