#ifndef TEXTURES_H
#define TEXTURES_H

#include <stdint.h>

#include "Map.h"
#include "SpriteIds.h"

struct Palette;

/*
 * Palettized texture stored column-major (transposed), like Wolf3D: texel
 * (u, v) is columns[u * height + v], so a wall column is one linear run.
 * Width and height are powers of two.
 */
struct Texture {
	int width;
	int height;
	uint8_t *columns;
};

static inline const uint8_t *texture_column(const Texture *tex, int u)
{
	return tex->columns + u * tex->height;
}

/*
 * Load an 8-bit PCX (version 5, RLE, 256-colour palette at the end) and
 * remap its colours onto `pal`. With `transparent`, pixels whose PCX colour
 * is pure magenta (255, 0, 255) become PAL_TRANSPARENT; every other pixel is
 * mapped to the closest entry in 0-254.
 */
bool texture_load_pcx(Texture *tex, const char *path, const Palette *pal, bool transparent);
void texture_free(Texture *tex);

/*
 * Wall textures indexed by tile: 1-9 = wall1..wall9.pcx, TILE_DOOR = door.pcx.
 * doorJamb (doorside.pcx) is drawn on walls seen from inside a door cell.
 */
struct WallTextures {
	Texture tile[TILE_DOOR + 1];
	Texture doorJamb;
};

bool textures_load_walls(WallTextures *walls, const Palette *pal);
void textures_free_walls(WallTextures *walls);

/* Floor and ceiling textures ("flats" in Doom), tiled once per map cell. */
struct FlatTextures {
	Texture floor;
	Texture ceiling;
};

bool textures_load_flats(FlatTextures *flats, const Palette *pal);
void textures_free_flats(FlatTextures *flats);

/* Sprite images indexed by SpriteId, loaded from assets/sprites/ with magenta as transparent. */
struct SpriteTextures {
	Texture sprite[SPR_COUNT];
};

bool textures_load_sprites(SpriteTextures *sprites, const Palette *pal);
void textures_free_sprites(SpriteTextures *sprites);

/* Texture to draw for a solid tile (secret walls look like wall 1). */
static inline const Texture *wall_texture(const WallTextures *walls, uint8_t tile)
{
	return &walls->tile[tile == TILE_SECRET ? TILE_WALL_FIRST : tile];
}

#endif
