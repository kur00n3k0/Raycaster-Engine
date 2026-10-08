#ifndef PALETTE_H
#define PALETTE_H

#include <stdint.h>

enum {
	PAL_COLORS = 256,
	PAL_TRANSPARENT = 255,	/* reserved: sprite transparency key */
	COLORMAP_LEVELS = 32,	/* 0 = full bright, 31 = nearly black */

	/* Flash palettes, Doom PLAYPAL style: 0 = normal, then red (damage), then gold (pickup). */
	PAL_FLASH_RED_FIRST = 1,
	PAL_FLASH_RED_COUNT = 8,
	PAL_FLASH_GOLD_FIRST = PAL_FLASH_RED_FIRST + PAL_FLASH_RED_COUNT,
	PAL_FLASH_GOLD_COUNT = 4,
	PAL_FLASH_COUNT = PAL_FLASH_GOLD_FIRST + PAL_FLASH_GOLD_COUNT
};

struct Palette {
	uint8_t rgb[PAL_COLORS * 3];
};

/*
 * Light tables, like Doom's COLORMAP: level[l][i] is the palette index that
 * best matches colour i darkened to brightness 1 - l / COLORMAP_LEVELS.
 * Shading a pixel is one table lookup. Index 255 always maps to itself.
 */
struct Colormaps {
	uint8_t level[COLORMAP_LEVELS][PAL_COLORS];
};

/*
 * Built-in palette used until a palette.pal is loaded: 16 ramps of 16 shades,
 * dark to bright. Ramp 0 is gray, ramps 1-15 walk around the hue wheel.
 * Index = ramp * 16 + shade. Index 255 is magenta so stray transparency shows.
 */
void palette_build_default(Palette *pal);

/* Closest palette entry to (r, g, b), never the reserved index 255. */
uint8_t palette_nearest(const Palette *pal, int r, int g, int b);

void palette_build_colormaps(const Palette *pal, Colormaps *maps);

/*
 * out[0] = base; out[1..8] tint towards red more and more; out[9..12]
 * towards gold. Swapping the palette in the shader is the whole effect:
 * the framebuffer and the light tables do not change.
 */
void palette_build_flashes(const Palette *base, Palette out[PAL_FLASH_COUNT]);

#endif
