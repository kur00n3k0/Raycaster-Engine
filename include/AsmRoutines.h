#ifndef ASMROUTINES_H
#define ASMROUTINES_H

#include <stddef.h>
#include <stdint.h>

/*
 * Inner loops shared between NASM and C++.
 *
 * rc_<name>      NASM version (src/asm/<name>.asm), only linked with RC_USE_ASM
 * rc_<name>_ref  C++ reference (src/Reference.cpp), always linked
 * RC_<name>      what engine code calls; picks one of the two at compile time
 *
 * Structs passed to ASM are mirrored by a NASM `struc` in the .asm file.
 * The static_asserts below pin every offset; change both sides together.
 */

/*
 * One textured vertical span (a wall column):
 *
 *   for i in 0 .. count-1:
 *       dest[i * pitch] = colormap[source[(frac >> 16) & mask]]
 *       frac += step
 *
 * frac and step are 16.16 fixed point texel positions and wrap mod 2^32.
 */
struct ColumnArgs {
	uint8_t *dest;			/* first framebuffer pixel to write */
	const uint8_t *source;		/* texture column, mask + 1 texels */
	const uint8_t *colormap;	/* 256-entry light table */
	int32_t pitch;			/* framebuffer bytes per row */
	int32_t count;			/* pixels to draw; <= 0 draws nothing */
	uint32_t frac;			/* 16.16 texel position of the first pixel */
	uint32_t step;			/* 16.16 texels per pixel */
	uint32_t mask;			/* texture height - 1 (power of two) */
	uint32_t pad;
};

static_assert(offsetof(ColumnArgs, dest) == 0, "ColumnArgs layout");
static_assert(offsetof(ColumnArgs, source) == 8, "ColumnArgs layout");
static_assert(offsetof(ColumnArgs, colormap) == 16, "ColumnArgs layout");
static_assert(offsetof(ColumnArgs, pitch) == 24, "ColumnArgs layout");
static_assert(offsetof(ColumnArgs, count) == 28, "ColumnArgs layout");
static_assert(offsetof(ColumnArgs, frac) == 32, "ColumnArgs layout");
static_assert(offsetof(ColumnArgs, step) == 36, "ColumnArgs layout");
static_assert(offsetof(ColumnArgs, mask) == 40, "ColumnArgs layout");
static_assert(sizeof(ColumnArgs) == 48, "ColumnArgs layout");

/*
 * One textured horizontal span (a floor or ceiling run):
 *
 *   for i in 0 .. count-1:
 *       u = (ufrac >> 16) & umask
 *       v = (vfrac >> 16) & vmask
 *       dest[i] = colormap[source[(u << vshift) | v]]
 *       ufrac += ustep
 *       vfrac += vstep
 *
 * source is a column-major texture (texel (u, v) at u * height + v), so
 * vshift = log2(height). Fractions wrap mod 2^32; steps may be negative
 * (two's complement).
 */
struct SpanArgs {
	uint8_t *dest;			/* first framebuffer pixel, drawn left to right */
	const uint8_t *source;		/* column-major texture */
	const uint8_t *colormap;	/* 256-entry light table */
	int32_t count;			/* pixels to draw; <= 0 draws nothing */
	uint32_t ufrac;			/* 16.16 texel u of the first pixel */
	uint32_t vfrac;			/* 16.16 texel v of the first pixel */
	uint32_t ustep;			/* 16.16 u per pixel */
	uint32_t vstep;			/* 16.16 v per pixel */
	uint32_t umask;			/* texture width - 1 */
	uint32_t vmask;			/* texture height - 1 */
	uint32_t vshift;		/* log2(texture height), 0-15 */
};

static_assert(offsetof(SpanArgs, dest) == 0, "SpanArgs layout");
static_assert(offsetof(SpanArgs, source) == 8, "SpanArgs layout");
static_assert(offsetof(SpanArgs, colormap) == 16, "SpanArgs layout");
static_assert(offsetof(SpanArgs, count) == 24, "SpanArgs layout");
static_assert(offsetof(SpanArgs, ufrac) == 28, "SpanArgs layout");
static_assert(offsetof(SpanArgs, vfrac) == 32, "SpanArgs layout");
static_assert(offsetof(SpanArgs, ustep) == 36, "SpanArgs layout");
static_assert(offsetof(SpanArgs, vstep) == 40, "SpanArgs layout");
static_assert(offsetof(SpanArgs, umask) == 44, "SpanArgs layout");
static_assert(offsetof(SpanArgs, vmask) == 48, "SpanArgs layout");
static_assert(offsetof(SpanArgs, vshift) == 52, "SpanArgs layout");
static_assert(sizeof(SpanArgs) == 56, "SpanArgs layout");

extern "C" {

/* Fill `count` bytes of the framebuffer with palette index `color`. */
void rc_fb_clear_ref(uint8_t *fb, uint32_t count, uint8_t color);

/* Draw one textured wall column, see ColumnArgs. */
void rc_draw_column_ref(const ColumnArgs *args);

/* Draw one textured floor/ceiling span, see SpanArgs. */
void rc_draw_span_ref(const SpanArgs *args);

/*
 * Draw one masked sprite column: same as rc_draw_column, but texels equal
 * to 255 (PAL_TRANSPARENT) are skipped and leave the framebuffer untouched.
 */
void rc_draw_sprite_col_ref(const ColumnArgs *args);

#if RC_USE_ASM
void rc_fb_clear(uint8_t *fb, uint32_t count, uint8_t color);
void rc_draw_column(const ColumnArgs *args);
void rc_draw_span(const SpanArgs *args);
void rc_draw_sprite_col(const ColumnArgs *args);
#endif

}

#if RC_USE_ASM
	#define RC_fb_clear rc_fb_clear
	#define RC_draw_column rc_draw_column
	#define RC_draw_span rc_draw_span
	#define RC_draw_sprite_col rc_draw_sprite_col
#else
	#define RC_fb_clear rc_fb_clear_ref
	#define RC_draw_column rc_draw_column_ref
	#define RC_draw_span rc_draw_span_ref
	#define RC_draw_sprite_col rc_draw_sprite_col_ref
#endif

#endif
