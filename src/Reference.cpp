#include "AsmRoutines.h"

/*
 * C++ reference versions of every ASM routine. These define the correct
 * output; the NASM versions must match them byte for byte.
 */

extern "C" void rc_fb_clear_ref(uint8_t *fb, uint32_t count, uint8_t color)
{
	for (uint32_t i = 0; i < count; i++)
		fb[i] = color;
}

extern "C" void rc_draw_column_ref(const ColumnArgs *args)
{
	uint8_t *dest = args->dest;
	const uint8_t *source = args->source;
	const uint8_t *colormap = args->colormap;
	uint32_t frac = args->frac;

	for (int32_t i = 0; i < args->count; i++) {
		*dest = colormap[source[(frac >> 16) & args->mask]];
		dest += args->pitch;
		frac += args->step;
	}
}

extern "C" void rc_draw_span_ref(const SpanArgs *args)
{
	uint8_t *dest = args->dest;
	const uint8_t *source = args->source;
	const uint8_t *colormap = args->colormap;
	uint32_t ufrac = args->ufrac;
	uint32_t vfrac = args->vfrac;

	for (int32_t i = 0; i < args->count; i++) {
		uint32_t u = (ufrac >> 16) & args->umask;
		uint32_t v = (vfrac >> 16) & args->vmask;
		dest[i] = colormap[source[(u << args->vshift) | v]];
		ufrac += args->ustep;
		vfrac += args->vstep;
	}
}

extern "C" void rc_draw_sprite_col_ref(const ColumnArgs *args)
{
	uint8_t *dest = args->dest;
	const uint8_t *source = args->source;
	const uint8_t *colormap = args->colormap;
	uint32_t frac = args->frac;

	for (int32_t i = 0; i < args->count; i++) {
		uint8_t texel = source[(frac >> 16) & args->mask];
		if (texel != 0xFF)
			*dest = colormap[texel];
		dest += args->pitch;
		frac += args->step;
	}
}
