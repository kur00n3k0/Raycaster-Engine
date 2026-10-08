#include "Map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { MAP_MAX_SIZE = 256 };

static bool tile_from_char(char c, uint8_t *tile)
{
	switch (c) {
	case '.': case 'P': case 'E': case '+': case 'a': case 'b': case 'l':
		*tile = TILE_EMPTY;
		return true;
	case '#':
		*tile = TILE_WALL_FIRST;
		return true;
	case 'D':
		*tile = TILE_DOOR;
		return true;
	case 'S':
		*tile = TILE_SECRET;
		return true;
	case 'X':
		*tile = TILE_EXIT;
		return true;
	default:
		if (c >= '1' && c <= '9') {
			*tile = (uint8_t)(c - '0');
			return true;
		}
		return false;
	}
}

/* ThingType for a map character, or -1 if it does not place a thing. */
static int thing_from_char(char c)
{
	switch (c) {
	case 'E': return THING_ENEMY;
	case '+': return THING_HEALTH;
	case 'a': return THING_AMMO;
	case 'b': return THING_BARREL;
	case 'l': return THING_LAMP;
	default:  return -1;
	}
}

bool map_music_name_ok(const char *name)
{
	size_t n = strlen(name);
	if (strcmp(name, "none") == 0)
		return true;
	return n > 4 && n < MAP_MUSIC_MAX && !strchr(name, '/') && strcasecmp(name + n - 4, ".mid") == 0;
}

bool map_next_name_ok(const char *name)
{
	size_t n = strlen(name);
	return n > 4 && n < MAP_NEXT_MAX && !strchr(name, '/') && strcasecmp(name + n - 4, ".txt") == 0;
}

/* "@name value" line before the grid. False on an unknown name or bad value. */
static bool apply_setting(Map *map, const char *path, int lineNo, char *line)
{
	char *name = line + 1;
	char *value = name + strcspn(name, " \t");
	if (*value)
		*value++ = '\0';
	value += strspn(value, " \t");
	char *end = value + strlen(value);
	while (end > value && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';

	if (strcmp(name, "music") == 0) {
		if (!map_music_name_ok(value)) {
			fprintf(stderr, "%s:%d: @music needs a .mid file name in %s, or none\n",
				path, lineNo, MAP_MUSIC_DIR);
			return false;
		}
		snprintf(map->music, sizeof(map->music), "%s", strcmp(value, "none") == 0 ? "" : value);
		return true;
	}
	if (strcmp(name, "next") == 0) {
		if (!map_next_name_ok(value)) {
			fprintf(stderr, "%s:%d: @next needs a .txt map file name in the same directory\n",
				path, lineNo);
			return false;
		}
		snprintf(map->next, sizeof(map->next), "%s", value);
		return true;
	}
	fprintf(stderr, "%s:%d: unknown map setting '@%s'\n", path, lineNo, name);
	return false;
}

bool map_load(Map *map, const char *path)
{
	memset(map, 0, sizeof(*map));
	snprintf(map->music, sizeof(map->music), "%s", MAP_MUSIC_DEFAULT);

	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Cannot open map %s\n", path);
		return false;
	}

	/* First pass: read lines into a fixed scratch grid. */
	static char grid[MAP_MAX_SIZE][MAP_MAX_SIZE + 2];
	static int rowLine[MAP_MAX_SIZE];	/* file line of each row, for messages */
	int rows = 0;
	int cols = 0;
	char line[MAP_MAX_SIZE + 2];
	int lineNo = 0;
	while (fgets(line, sizeof(line), f)) {
		lineNo++;
		size_t len = strcspn(line, "\r\n");
		line[len] = '\0';
		if (len == 0)
			continue;
		if (line[0] == '@') {
			if (rows > 0) {
				fprintf(stderr, "%s:%d: map settings must come before the grid\n", path, lineNo);
				fclose(f);
				return false;
			}
			if (!apply_setting(map, path, lineNo, line)) {
				fclose(f);
				return false;
			}
			continue;
		}
		if (rows == MAP_MAX_SIZE || len > MAP_MAX_SIZE) {
			fprintf(stderr, "%s: map larger than %dx%d\n", path, MAP_MAX_SIZE, MAP_MAX_SIZE);
			fclose(f);
			return false;
		}
		if (rows == 0) {
			cols = (int)len;
		} else if ((int)len != cols) {
			fprintf(stderr, "%s:%d: row is %d wide, expected %d\n", path, lineNo, (int)len, cols);
			fclose(f);
			return false;
		}
		memcpy(grid[rows], line, len + 1);
		rowLine[rows] = lineNo;
		rows++;
	}
	fclose(f);

	if (rows < 3 || cols < 3) {
		fprintf(stderr, "%s: map must be at least 3x3\n", path);
		return false;
	}

	map->width = cols;
	map->height = rows;
	map->tiles = (uint8_t *)malloc((size_t)cols * (size_t)rows);
	map->doorIndex = (uint8_t *)malloc((size_t)cols * (size_t)rows);
	memset(map->doorIndex, MAP_NO_DOOR, (size_t)cols * (size_t)rows);

	bool havePlayer = false;
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			char c = grid[y][x];
			uint8_t tile;
			if (!tile_from_char(c, &tile)) {
				fprintf(stderr, "%s:%d:%d: unknown map character '%c'\n", path, rowLine[y], x + 1, c);
				map_free(map);
				return false;
			}
			bool border = x == 0 || y == 0 || x == cols - 1 || y == rows - 1;
			if (border && (tile == TILE_EMPTY || tile == TILE_DOOR || tile == TILE_SECRET)) {
				fprintf(stderr, "%s:%d:%d: map border must be solid wall\n", path, rowLine[y], x + 1);
				map_free(map);
				return false;
			}
			if (c == 'P') {
				if (havePlayer) {
					fprintf(stderr, "%s:%d:%d: more than one player start\n", path, rowLine[y], x + 1);
					map_free(map);
					return false;
				}
				havePlayer = true;
				map->playerStart = glm::vec2((float)x + 0.5f, (float)y + 0.5f);
				map->playerAngle = 0.0f;
			}
			int thing = thing_from_char(c);
			if (thing >= 0) {
				if (map->thingCount == MAP_MAX_THINGS) {
					fprintf(stderr, "%s: more than %d things\n", path, MAP_MAX_THINGS);
					map_free(map);
					return false;
				}
				Thing *t = &map->things[map->thingCount++];
				t->type = (uint8_t)thing;
				t->pos = glm::vec2((float)x + 0.5f, (float)y + 0.5f);
			}
			map->tiles[y * cols + x] = tile;
		}
	}

	/* Doors need walls on two opposite sides; that decides which way they face. */
	for (int y = 1; y < rows - 1; y++) {
		for (int x = 1; x < cols - 1; x++) {
			if (map->tiles[y * cols + x] != TILE_DOOR)
				continue;
			bool wallsNS = map->tiles[(y - 1) * cols + x] != TILE_EMPTY
				&& map->tiles[(y + 1) * cols + x] != TILE_EMPTY;
			bool wallsEW = map->tiles[y * cols + x - 1] != TILE_EMPTY
				&& map->tiles[y * cols + x + 1] != TILE_EMPTY;
			if (wallsNS == wallsEW) {
				fprintf(stderr, "%s:%d:%d: door needs walls on exactly two opposite sides\n",
					path, rowLine[y], x + 1);
				map_free(map);
				return false;
			}
			if (map->doorCount == MAP_MAX_DOORS) {
				fprintf(stderr, "%s: more than %d doors\n", path, MAP_MAX_DOORS);
				map_free(map);
				return false;
			}
			Door *d = &map->doors[map->doorCount];
			d->x = x;
			d->y = y;
			d->vertical = wallsNS;
			d->state = DOOR_CLOSED;
			d->open = 0.0f;
			d->timer = 0.0f;
			map->doorIndex[y * cols + x] = (uint8_t)map->doorCount;
			map->doorCount++;
		}
	}

	if (!havePlayer) {
		fprintf(stderr, "%s: no player start 'P'\n", path);
		map_free(map);
		return false;
	}
	return true;
}

void map_free(Map *map)
{
	free(map->tiles);
	free(map->doorIndex);
	map->tiles = nullptr;
	map->doorIndex = nullptr;
	map->doorCount = 0;
	map->width = 0;
	map->height = 0;
	map->thingCount = 0;
}
