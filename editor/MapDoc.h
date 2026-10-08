#ifndef MAPDOC_H
#define MAPDOC_H

#include <stdint.h>

#include <vector>

/*
 * The map being edited: the same character grid as the .txt map file
 * (README.txt, MAP FORMAT), plus undo/redo and validation. No ImGui and no
 * GL in here. Wall texture 1 can be '#' or '1'; cells keep what the file had,
 * and the Wall 1 brush paints '#'.
 *
 * Validation mirrors map_load() in src/Map.cpp: anything it reports as an
 * error would also stop the game from loading the map.
 */

enum {
	DOC_MIN_SIZE = 3,
	DOC_MAX_SIZE = 256,	/* MAP_MAX_SIZE in Map.cpp */
	DOC_MAX_DOORS = 64,	/* MAP_MAX_DOORS */
	DOC_MAX_THINGS = 256,	/* MAP_MAX_THINGS */
	DOC_UNDO_LEVELS = 256
};

enum { DOC_MUSIC_MAX = 64 };	/* MAP_MUSIC_MAX */

struct DocState {
	int width;
	int height;
	std::vector<char> cells;	/* width * height, row-major */
	char music[DOC_MUSIC_MAX];
};

struct MapDoc {
	int width;
	int height;
	std::vector<char> cells;
	/*
	 * @music line: "" = none written (the game plays MAP_MUSIC_DEFAULT),
	 * "none", or a .mid file name in assets/music/.
	 */
	char music[DOC_MUSIC_MAX];
	char path[512];			/* "" = not saved yet */
	bool dirty;
	uint32_t revision;		/* bumped on every change, to know when to revalidate */

	std::vector<DocState> undo;
	std::vector<DocState> redo;
	bool editing;			/* between doc_begin_edit and doc_end_edit */
};

struct Problem {
	int x, y;			/* cell, or -1 for the whole map */
	bool error;			/* false = warning (the game still loads it) */
	char text[96];
};

struct DocStats {
	int enemies, health, ammo, barrels, lamps;
	int doors, secrets, players;
	int errors, warnings;
};

/* Cell classes, matching Map.cpp's tile_from_char / thing_from_char. */
static inline bool cell_is_wall(char c) { return c == '#' || (c >= '1' && c <= '9'); }
static inline bool cell_is_thing(char c) { return c == 'E' || c == '+' || c == 'a' || c == 'b' || c == 'l'; }
/* Non-empty tile in the game's sense: walls, doors, secret walls. */
static inline bool cell_is_solid(char c) { return cell_is_wall(c) || c == 'D' || c == 'S'; }
bool cell_is_valid(char c);

/* New map: solid border, floor inside, player start at (1, 1). */
void doc_new(MapDoc *doc, int width, int height);

/* Loads a map file. Short rows are padded with wall, unknown characters become floor (noted in err). */
bool doc_load(MapDoc *doc, const char *path, char *err, int errSize);
bool doc_save(MapDoc *doc, const char *path);

static inline bool doc_inside(const MapDoc *doc, int x, int y)
{
	return x >= 0 && y >= 0 && x < doc->width && y < doc->height;
}

static inline char doc_get(const MapDoc *doc, int x, int y)
{
	return doc_inside(doc, x, y) ? doc->cells[y * doc->width + x] : '#';
}

/* Changes the @music setting ("" = default). */
void doc_set_music(MapDoc *doc, const char *music);

/* Sets one cell. 'P' moves the player start (there is only one). Outside cells are ignored. */
void doc_set(MapDoc *doc, int x, int y, char c);

/* Replace the 4-connected region of identical cells around (x, y). */
void doc_flood_fill(MapDoc *doc, int x, int y, char c);

/* Make every border cell that is not a wall into wall texture 1. */
void doc_wall_border(MapDoc *doc);

/*
 * New size; the old grid lands at (offsetX, offsetY) in the new one (can be
 * negative to crop from the left/top). New cells are floor.
 */
void doc_resize(MapDoc *doc, int width, int height, int offsetX, int offsetY);

/*
 * Undo: wrap every user action in begin/end. end drops the snapshot if
 * nothing changed, so clicks that do nothing do not fill the history.
 */
void doc_begin_edit(MapDoc *doc);
void doc_end_edit(MapDoc *doc);
bool doc_undo(MapDoc *doc);
bool doc_redo(MapDoc *doc);

/*
 * Door orientation as the game decides it: walls north and south = vertical
 * panel (passage east-west). Returns false if the door is invalid.
 */
bool doc_door_vertical(const MapDoc *doc, int x, int y, bool *vertical);

/*
 * Fills problems (errors first) and stats. reach (width * height) gets 1 for
 * every cell the player can walk to from the start through floors, doors and
 * secret walls; all 0 if there is no single start.
 */
void doc_validate(const MapDoc *doc, std::vector<Problem> *problems, DocStats *stats,
	std::vector<uint8_t> *reach);

#endif
