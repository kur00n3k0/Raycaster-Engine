#include "Palette.h"

static uint8_t to_byte(float v)
{
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	return (uint8_t)(v * 255.0f + 0.5f);
}

/* h in [0, 6), s and v in [0, 1] */
static void hsv_to_rgb(float h, float s, float v, uint8_t *out)
{
	int sector = (int)h;
	float f = h - (float)sector;
	float p = v * (1.0f - s);
	float q = v * (1.0f - s * f);
	float t = v * (1.0f - s * (1.0f - f));
	float r, g, b;
	switch (sector) {
	case 0:  r = v; g = t; b = p; break;
	case 1:  r = q; g = v; b = p; break;
	case 2:  r = p; g = v; b = t; break;
	case 3:  r = p; g = q; b = v; break;
	case 4:  r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	out[0] = to_byte(r);
	out[1] = to_byte(g);
	out[2] = to_byte(b);
}

void palette_build_default(Palette *pal)
{
	for (int ramp = 0; ramp < 16; ramp++) {
		for (int shade = 0; shade < 16; shade++) {
			uint8_t *c = &pal->rgb[(ramp * 16 + shade) * 3];
			float v = (float)(shade + 1) / 16.0f;
			if (ramp == 0) {
				c[0] = c[1] = c[2] = to_byte((float)shade / 15.0f);
			} else {
				float h = (float)(ramp - 1) * 6.0f / 15.0f;
				hsv_to_rgb(h, 0.85f, v, c);
			}
		}
	}

	uint8_t *key = &pal->rgb[PAL_TRANSPARENT * 3];
	key[0] = 255;
	key[1] = 0;
	key[2] = 255;
}

uint8_t palette_nearest(const Palette *pal, int r, int g, int b)
{
	int best = 0;
	int bestDist = 0x7fffffff;
	for (int i = 0; i < PAL_TRANSPARENT; i++) {
		const uint8_t *c = &pal->rgb[i * 3];
		int dr = c[0] - r;
		int dg = c[1] - g;
		int db = c[2] - b;
		int dist = dr * dr + dg * dg + db * db;
		if (dist < bestDist) {
			bestDist = dist;
			best = i;
			if (dist == 0)
				break;
		}
	}
	return (uint8_t)best;
}

void palette_build_colormaps(const Palette *pal, Colormaps *maps)
{
	for (int l = 0; l < COLORMAP_LEVELS; l++) {
		/* 16.16 brightness factor */
		int f = ((COLORMAP_LEVELS - l) << 16) / COLORMAP_LEVELS;
		for (int i = 0; i < PAL_COLORS; i++) {
			if (i == PAL_TRANSPARENT) {
				maps->level[l][i] = PAL_TRANSPARENT;
				continue;
			}
			if (l == 0) {
				maps->level[l][i] = (uint8_t)i;
				continue;
			}
			const uint8_t *c = &pal->rgb[i * 3];
			maps->level[l][i] = palette_nearest(pal,
				(c[0] * f) >> 16, (c[1] * f) >> 16, (c[2] * f) >> 16);
		}
	}
}

static void tint(const Palette *base, Palette *out, int r, int g, int b, float amount)
{
	for (int i = 0; i < PAL_COLORS; i++) {
		const uint8_t *c = &base->rgb[i * 3];
		uint8_t *o = &out->rgb[i * 3];
		o[0] = (uint8_t)((float)c[0] + ((float)r - (float)c[0]) * amount + 0.5f);
		o[1] = (uint8_t)((float)c[1] + ((float)g - (float)c[1]) * amount + 0.5f);
		o[2] = (uint8_t)((float)c[2] + ((float)b - (float)c[2]) * amount + 0.5f);
	}
}

void palette_build_flashes(const Palette *base, Palette out[PAL_FLASH_COUNT])
{
	out[0] = *base;
	for (int i = 0; i < PAL_FLASH_RED_COUNT; i++)
		tint(base, &out[PAL_FLASH_RED_FIRST + i], 255, 0, 0, 0.1f * (float)(i + 1));
	for (int i = 0; i < PAL_FLASH_GOLD_COUNT; i++)
		tint(base, &out[PAL_FLASH_GOLD_FIRST + i], 215, 186, 69, 0.125f * (float)(i + 1));
}
