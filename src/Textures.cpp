#include "Textures.h"

#include "Palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ZSoft PCX header, 128 bytes, little-endian. Read field by field, no struct packing. */
enum {
	PCX_HEADER_SIZE = 128,
	PCX_PALETTE_SIZE = 769,		/* 0x0C marker + 256 RGB triplets */
	PCX_MAX_SIZE = 4096
};

static int read_u16(const uint8_t *p)
{
	return p[0] | (p[1] << 8);
}

static bool is_pow2(int v)
{
	return v > 0 && (v & (v - 1)) == 0;
}

static uint8_t *read_file(const char *path, long *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return nullptr;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *data = (uint8_t *)malloc((size_t)*size);
	if (data && fread(data, 1, (size_t)*size, f) != (size_t)*size) {
		free(data);
		data = nullptr;
	}
	fclose(f);
	return data;
}

bool texture_load_pcx(Texture *tex, const char *path, const Palette *pal, bool transparent)
{
	memset(tex, 0, sizeof(*tex));

	long size = 0;
	uint8_t *file = read_file(path, &size);
	if (!file) {
		fprintf(stderr, "Cannot read %s\n", path);
		return false;
	}

	const uint8_t *h = file;
	if (size < PCX_HEADER_SIZE + PCX_PALETTE_SIZE || h[0] != 0x0A || h[2] != 1 || h[3] != 8 || h[65] != 1) {
		fprintf(stderr, "%s: not an 8-bit single-plane RLE PCX\n", path);
		free(file);
		return false;
	}
	int width = read_u16(h + 8) - read_u16(h + 4) + 1;
	int height = read_u16(h + 10) - read_u16(h + 6) + 1;
	int bytesPerLine = read_u16(h + 66);
	if (!is_pow2(width) || !is_pow2(height) || width > PCX_MAX_SIZE || height > PCX_MAX_SIZE
		|| bytesPerLine < width) {
		fprintf(stderr, "%s: %dx%d, textures must be power-of-two sized\n", path, width, height);
		free(file);
		return false;
	}

	const uint8_t *palData = file + size - PCX_PALETTE_SIZE;
	if (palData[0] != 0x0C) {
		fprintf(stderr, "%s: missing 256-colour palette\n", path);
		free(file);
		return false;
	}
	palData++;

	/* Decode RLE: a byte with the top two bits set is a run count for the next byte. */
	size_t total = (size_t)bytesPerLine * (size_t)height;
	uint8_t *image = (uint8_t *)malloc(total);
	const uint8_t *src = file + PCX_HEADER_SIZE;
	const uint8_t *srcEnd = file + size - PCX_PALETTE_SIZE;
	size_t out = 0;
	while (out < total && src < srcEnd) {
		uint8_t b = *src++;
		int run = 1;
		if ((b & 0xC0) == 0xC0) {
			run = b & 0x3F;
			if (src == srcEnd)
				break;
			b = *src++;
		}
		while (run-- > 0 && out < total)
			image[out++] = b;
	}
	if (out < total) {
		fprintf(stderr, "%s: truncated image data\n", path);
		free(image);
		free(file);
		return false;
	}

	/* Map the file's palette onto the game palette. */
	uint8_t remap[PAL_COLORS];
	for (int i = 0; i < PAL_COLORS; i++) {
		const uint8_t *c = palData + i * 3;
		const uint8_t *g = &pal->rgb[i * 3];
		if (transparent && c[0] == 255 && c[1] == 0 && c[2] == 255)
			remap[i] = PAL_TRANSPARENT;
		else if (i != PAL_TRANSPARENT && c[0] == g[0] && c[1] == g[1] && c[2] == g[2])
			remap[i] = (uint8_t)i;
		else
			remap[i] = palette_nearest(pal, c[0], c[1], c[2]);
	}

	/* Transpose into column-major storage. */
	tex->width = width;
	tex->height = height;
	tex->columns = (uint8_t *)malloc((size_t)width * (size_t)height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++)
			tex->columns[x * height + y] = remap[image[y * bytesPerLine + x]];
	}

	free(image);
	free(file);
	return true;
}

void texture_free(Texture *tex)
{
	free(tex->columns);
	memset(tex, 0, sizeof(*tex));
}

bool textures_load_walls(WallTextures *walls, const Palette *pal)
{
	memset(walls, 0, sizeof(*walls));
	char path[64];
	for (int t = TILE_WALL_FIRST; t <= TILE_DOOR; t++) {
		if (t == TILE_DOOR)
			snprintf(path, sizeof(path), "assets/textures/door.pcx");
		else
			snprintf(path, sizeof(path), "assets/textures/wall%d.pcx", t);
		if (!texture_load_pcx(&walls->tile[t], path, pal, false)) {
			textures_free_walls(walls);
			return false;
		}
	}
	if (!texture_load_pcx(&walls->tile[TILE_EXIT], "assets/textures/exit.pcx", pal, false)) {
		textures_free_walls(walls);
		return false;
	}
	if (!texture_load_pcx(&walls->doorJamb, "assets/textures/doorside.pcx", pal, false)) {
		textures_free_walls(walls);
		return false;
	}
	return true;
}

void textures_free_walls(WallTextures *walls)
{
	for (int t = 0; t <= TILE_EXIT; t++)
		texture_free(&walls->tile[t]);
	texture_free(&walls->doorJamb);
}

bool textures_load_flats(FlatTextures *flats, const Palette *pal)
{
	memset(flats, 0, sizeof(*flats));
	if (!texture_load_pcx(&flats->floor, "assets/textures/floor.pcx", pal, false)
		|| !texture_load_pcx(&flats->ceiling, "assets/textures/ceiling.pcx", pal, false)) {
		textures_free_flats(flats);
		return false;
	}
	return true;
}

void textures_free_flats(FlatTextures *flats)
{
	texture_free(&flats->floor);
	texture_free(&flats->ceiling);
}

bool textures_load_sprites(SpriteTextures *sprites, const Palette *pal)
{
	static const char *const SPRITE_NAMES[SPR_COUNT] = {
		"enemy_stand", "enemy_walk1", "enemy_walk2", "enemy_shoot", "enemy_pain", "enemy_dead",
		"health", "ammo", "barrel", "lamp", "weapon_idle", "weapon_fire"
	};

	memset(sprites, 0, sizeof(*sprites));
	char path[64];
	for (int t = 0; t < SPR_COUNT; t++) {
		snprintf(path, sizeof(path), "assets/sprites/%s.pcx", SPRITE_NAMES[t]);
		if (!texture_load_pcx(&sprites->sprite[t], path, pal, true)) {
			textures_free_sprites(sprites);
			return false;
		}
	}
	return true;
}

void textures_free_sprites(SpriteTextures *sprites)
{
	for (int t = 0; t < SPR_COUNT; t++)
		texture_free(&sprites->sprite[t]);
}
