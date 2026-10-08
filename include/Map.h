#ifndef MAP_H
#define MAP_H

#include <stdint.h>

#include <glm/vec2.hpp>

/*
 * Tile grid loaded from a plain-text map (see README.txt, MAP FORMAT).
 * Cell (x, y) covers world [x, x+1) x [y, y+1). x grows east, y grows south
 * (row 0 of the file is the north edge).
 */

enum Tile : uint8_t {
	TILE_EMPTY = 0,
	/* 1-9: solid wall with texture 1-9 */
	TILE_WALL_FIRST = 1,
	TILE_WALL_LAST = 9,
	TILE_DOOR = 10,
	TILE_SECRET = 11,	/* push wall; looks like wall texture 1 */
	TILE_PUSHWALL = 12,	/* cell covered by the moving push wall (see Pushwall) */
	TILE_EXIT = 13		/* exit door: solid; using it finishes the level */
};

/*
 * Sliding door, Wolf3D style: a thin panel across the middle of its cell.
 * A vertical door (walls north and south, passage east-west) sits on the
 * plane x = cell + 0.5 and slides towards +y; a horizontal one sits on
 * y = cell + 0.5 and slides towards +x.
 */
enum DoorState : uint8_t {
	DOOR_CLOSED,
	DOOR_OPENING,
	DOOR_OPEN,
	DOOR_CLOSING
};

enum { MAP_MAX_DOORS = 64, MAP_NO_DOOR = 0xFF };

struct Door {
	int x, y;
	bool vertical;
	uint8_t state;		/* DoorState */
	float open;		/* 0 = closed, 1 = fully open (passable) */
	float timer;		/* seconds left before an open door starts closing */
};

/*
 * The one moving push wall (Wolf3D also allows only one at a time): a 1x1
 * block whose top-left corner is at (x, y) + (dx, dy) * offset. While moving
 * it covers two cells, both marked TILE_PUSHWALL.
 */
struct Pushwall {
	bool active;
	int x, y;		/* cell it is leaving */
	int dx, dy;		/* unit direction of travel */
	float offset;		/* 0 .. 1 into the next cell */
	int moved;		/* cells already travelled */
};

/* Things placed by the map (Doom's term): items, decorations, enemies. */
enum ThingType : uint8_t {
	THING_ENEMY,	/* E */
	THING_HEALTH,	/* + */
	THING_AMMO,	/* a */
	THING_BARREL,	/* b */
	THING_LAMP,	/* l */
	THING_TYPE_COUNT
};

enum { MAP_MAX_THINGS = 256 };

/*
 * Per-map settings come before the grid as "@name value" lines ('@' is not a
 * map character). @music picks the song: a .mid file name in MAP_MUSIC_DIR, or
 * "none". Without it the map gets MAP_MUSIC_DEFAULT. @next names the map that
 * follows when the exit is used: a .txt file in the same directory as this
 * map. Without it this is the last level of the episode.
 */
enum { MAP_MUSIC_MAX = 64, MAP_NEXT_MAX = 64 };
#define MAP_MUSIC_DIR "assets/music/"
#define MAP_MUSIC_DEFAULT "e1m1.mid"

struct Thing {
	uint8_t type;		/* ThingType */
	glm::vec2 pos;		/* centre of its cell */
};

struct Map {
	int width;
	int height;
	uint8_t *tiles;		/* width * height, row-major */

	Thing things[MAP_MAX_THINGS];
	int thingCount;

	Door doors[MAP_MAX_DOORS];
	int doorCount;
	uint8_t *doorIndex;	/* width * height, index into doors or MAP_NO_DOOR */

	Pushwall pushwall;

	glm::vec2 playerStart;	/* centre of the 'P' cell */
	float playerAngle;	/* radians, 0 = east, grows clockwise (towards +y) */

	char music[MAP_MUSIC_MAX];	/* file in MAP_MUSIC_DIR, "" = no music */
	char next[MAP_NEXT_MAX];	/* next map's file name, "" = last level */
};

/* A valid @music value: "none" or a plain .mid file name (no directories). */
bool map_music_name_ok(const char *name);

/* A valid @next value: a plain .txt file name (no directories). */
bool map_next_name_ok(const char *name);

/* Returns false and prints why on a malformed map. */
bool map_load(Map *map, const char *path);
void map_free(Map *map);

static inline uint8_t map_tile(const Map *map, int x, int y)
{
	if (x < 0 || y < 0 || x >= map->width || y >= map->height)
		return TILE_WALL_FIRST;
	return map->tiles[y * map->width + x];
}

static inline Door *map_door(Map *map, int x, int y)
{
	uint8_t i = map->doorIndex[y * map->width + x];
	return i == MAP_NO_DOOR ? nullptr : &map->doors[i];
}

static inline const Door *map_door(const Map *map, int x, int y)
{
	uint8_t i = map->doorIndex[y * map->width + x];
	return i == MAP_NO_DOOR ? nullptr : &map->doors[i];
}

/* Does the cell block movement? Doors block until fully open. */
static inline bool map_blocks(const Map *map, int x, int y)
{
	uint8_t tile = map_tile(map, x, y);
	if (tile == TILE_EMPTY)
		return false;
	if (tile == TILE_DOOR)
		return map_door(map, x, y)->open < 1.0f;
	return true;
}

#endif
