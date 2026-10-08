#include "MapDoc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

bool cell_is_valid(char c)
{
	return cell_is_solid(c) || cell_is_thing(c) || c == '.' || c == 'P';
}

static void changed(MapDoc *doc)
{
	doc->dirty = true;
	doc->revision++;
}

static void clear_history(MapDoc *doc)
{
	doc->undo.clear();
	doc->redo.clear();
	doc->editing = false;
}

void doc_new(MapDoc *doc, int width, int height)
{
	doc->width = width;
	doc->height = height;
	doc->cells.assign((size_t)width * (size_t)height, '.');
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (x == 0 || y == 0 || x == width - 1 || y == height - 1)
				doc->cells[y * width + x] = '#';
		}
	}
	doc->cells[1 * width + 1] = 'P';
	doc->path[0] = '\0';
	doc->dirty = false;
	doc->revision++;
	clear_history(doc);
}

bool doc_load(MapDoc *doc, const char *path, char *err, int errSize)
{
	err[0] = '\0';
	FILE *f = fopen(path, "rb");
	if (!f) {
		snprintf(err, (size_t)errSize, "Cannot open %s", path);
		return false;
	}

	/* Same rules as map_load: one row per non-empty line. */
	std::vector<std::vector<char>> rows;
	int width = 0;
	char line[1024];
	bool tooLong = false;
	while (fgets(line, sizeof(line), f)) {
		size_t len = strcspn(line, "\r\n");
		if (len == 0)
			continue;
		if (len > DOC_MAX_SIZE || rows.size() == DOC_MAX_SIZE) {
			tooLong = true;
			break;
		}
		rows.emplace_back(line, line + len);
		if ((int)len > width)
			width = (int)len;
	}
	fclose(f);
	if (tooLong) {
		snprintf(err, (size_t)errSize, "%s is larger than %dx%d", path, DOC_MAX_SIZE, DOC_MAX_SIZE);
		return false;
	}
	int height = (int)rows.size();
	if (width < DOC_MIN_SIZE || height < DOC_MIN_SIZE) {
		snprintf(err, (size_t)errSize, "%s is smaller than %dx%d", path, DOC_MIN_SIZE, DOC_MIN_SIZE);
		return false;
	}

	/* Repair what the game would reject outright, and say so. */
	int padded = 0, unknown = 0;
	doc->width = width;
	doc->height = height;
	doc->cells.assign((size_t)width * (size_t)height, '#');
	for (int y = 0; y < height; y++) {
		const std::vector<char> &row = rows[(size_t)y];
		if ((int)row.size() < width)
			padded++;
		for (int x = 0; x < (int)row.size(); x++) {
			char c = row[(size_t)x];
			if (!cell_is_valid(c)) {
				c = '.';
				unknown++;
			}
			doc->cells[y * width + x] = c;
		}
	}
	snprintf(doc->path, sizeof(doc->path), "%s", path);
	doc->dirty = padded > 0 || unknown > 0;
	doc->revision++;
	clear_history(doc);
	if (padded || unknown)
		snprintf(err, (size_t)errSize, "Repaired on load: %d short rows padded with wall, "
			"%d unknown characters made floor", padded, unknown);
	return true;
}

bool doc_save(MapDoc *doc, const char *path)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	for (int y = 0; y < doc->height; y++) {
		fwrite(&doc->cells[(size_t)y * (size_t)doc->width], 1, (size_t)doc->width, f);
		fputc('\n', f);
	}
	bool ok = fclose(f) == 0;
	if (ok) {
		if (path != doc->path)
			snprintf(doc->path, sizeof(doc->path), "%s", path);
		doc->dirty = false;
	}
	return ok;
}

void doc_set(MapDoc *doc, int x, int y, char c)
{
	if (!doc_inside(doc, x, y))
		return;
	char *cell = &doc->cells[y * doc->width + x];
	if (*cell == c)
		return;
	if (c == 'P') {
		for (char &other : doc->cells) {
			if (other == 'P')
				other = '.';
		}
	}
	*cell = c;
	changed(doc);
}

void doc_flood_fill(MapDoc *doc, int x, int y, char c)
{
	if (!doc_inside(doc, x, y))
		return;
	char from = doc_get(doc, x, y);
	if (from == c)
		return;
	if (c == 'P') {
		doc_set(doc, x, y, c);	/* one start only: filling with it is a single click */
		return;
	}

	std::vector<int> stack;
	stack.push_back(y * doc->width + x);
	while (!stack.empty()) {
		int i = stack.back();
		stack.pop_back();
		if (doc->cells[(size_t)i] != from)
			continue;
		doc->cells[(size_t)i] = c;
		int cx = i % doc->width, cy = i / doc->width;
		if (cx > 0)			stack.push_back(i - 1);
		if (cx < doc->width - 1)	stack.push_back(i + 1);
		if (cy > 0)			stack.push_back(i - doc->width);
		if (cy < doc->height - 1)	stack.push_back(i + doc->width);
	}
	changed(doc);
}

void doc_wall_border(MapDoc *doc)
{
	for (int y = 0; y < doc->height; y++) {
		for (int x = 0; x < doc->width; x++) {
			bool border = x == 0 || y == 0 || x == doc->width - 1 || y == doc->height - 1;
			if (border && !cell_is_wall(doc_get(doc, x, y)))
				doc_set(doc, x, y, '#');
		}
	}
}

void doc_resize(MapDoc *doc, int width, int height, int offsetX, int offsetY)
{
	if (width == doc->width && height == doc->height && offsetX == 0 && offsetY == 0)
		return;
	std::vector<char> cells((size_t)width * (size_t)height, '.');
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int ox = x - offsetX, oy = y - offsetY;
			if (doc_inside(doc, ox, oy))
				cells[(size_t)(y * width + x)] = doc_get(doc, ox, oy);
		}
	}
	doc->width = width;
	doc->height = height;
	doc->cells.swap(cells);
	changed(doc);
}

static DocState snapshot(const MapDoc *doc)
{
	DocState s;
	s.width = doc->width;
	s.height = doc->height;
	s.cells = doc->cells;
	return s;
}

static void restore(MapDoc *doc, DocState *s)
{
	doc->width = s->width;
	doc->height = s->height;
	doc->cells.swap(s->cells);
	changed(doc);
}

void doc_begin_edit(MapDoc *doc)
{
	if (doc->editing)
		return;
	doc->undo.push_back(snapshot(doc));
	if (doc->undo.size() > DOC_UNDO_LEVELS)
		doc->undo.erase(doc->undo.begin());
	doc->editing = true;
}

void doc_end_edit(MapDoc *doc)
{
	if (!doc->editing)
		return;
	doc->editing = false;
	const DocState &before = doc->undo.back();
	if (before.width == doc->width && before.height == doc->height && before.cells == doc->cells) {
		doc->undo.pop_back();
		return;
	}
	doc->redo.clear();
}

bool doc_undo(MapDoc *doc)
{
	doc_end_edit(doc);
	if (doc->undo.empty())
		return false;
	doc->redo.push_back(snapshot(doc));
	restore(doc, &doc->undo.back());
	doc->undo.pop_back();
	return true;
}

bool doc_redo(MapDoc *doc)
{
	doc_end_edit(doc);
	if (doc->redo.empty())
		return false;
	doc->undo.push_back(snapshot(doc));
	restore(doc, &doc->redo.back());
	doc->redo.pop_back();
	return true;
}

bool doc_door_vertical(const MapDoc *doc, int x, int y, bool *vertical)
{
	/* Map.cpp: any non-empty tile counts as a wall here, doors and secret walls too. */
	bool wallsNS = cell_is_solid(doc_get(doc, x, y - 1)) && cell_is_solid(doc_get(doc, x, y + 1));
	bool wallsEW = cell_is_solid(doc_get(doc, x - 1, y)) && cell_is_solid(doc_get(doc, x + 1, y));
	*vertical = wallsNS;
	return wallsNS != wallsEW;
}

static void add(std::vector<Problem> *problems, int x, int y, bool error, const char *fmt, ...)
{
	Problem p;
	p.x = x;
	p.y = y;
	p.error = error;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(p.text, sizeof(p.text), fmt, ap);
	va_end(ap);
	problems->push_back(p);
}

void doc_validate(const MapDoc *doc, std::vector<Problem> *problems, DocStats *stats,
	std::vector<uint8_t> *reach)
{
	problems->clear();
	memset(stats, 0, sizeof(*stats));
	const int w = doc->width, h = doc->height;
	int playerX = -1, playerY = -1;

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			char c = doc_get(doc, x, y);
			bool border = x == 0 || y == 0 || x == w - 1 || y == h - 1;
			if (border && !cell_is_wall(c))
				add(problems, x, y, true, "Border must be solid wall");
			switch (c) {
			case 'E': stats->enemies++; break;
			case '+': stats->health++; break;
			case 'a': stats->ammo++; break;
			case 'b': stats->barrels++; break;
			case 'l': stats->lamps++; break;
			case 'S': stats->secrets++; break;
			case 'P':
				stats->players++;
				playerX = x;
				playerY = y;
				break;
			case 'D': {
				stats->doors++;
				bool vertical;
				if (!border && !doc_door_vertical(doc, x, y, &vertical))
					add(problems, x, y, true, "Door needs walls on exactly two opposite sides");
				break;
			}
			}
		}
	}

	int things = stats->enemies + stats->health + stats->ammo + stats->barrels + stats->lamps;
	if (stats->players == 0)
		add(problems, -1, -1, true, "No player start (P)");
	if (stats->players > 1)
		add(problems, -1, -1, true, "More than one player start");
	if (stats->doors > DOC_MAX_DOORS)
		add(problems, -1, -1, true, "%d doors, the game allows %d", stats->doors, DOC_MAX_DOORS);
	if (things > DOC_MAX_THINGS)
		add(problems, -1, -1, true, "%d things, the game allows %d", things, DOC_MAX_THINGS);

	/* Reachability: flood from the start through everything that is not plain wall. */
	reach->assign((size_t)w * (size_t)h, 0);
	if (stats->players == 1) {
		std::vector<int> stack;
		stack.push_back(playerY * w + playerX);
		(*reach)[(size_t)stack.back()] = 1;
		while (!stack.empty()) {
			int i = stack.back();
			stack.pop_back();
			int cx = i % w, cy = i / w;
			static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
			for (int d = 0; d < 4; d++) {
				int nx = cx + DX[d], ny = cy + DY[d];
				if (!doc_inside(doc, nx, ny) || cell_is_wall(doc_get(doc, nx, ny)))
					continue;
				int n = ny * w + nx;
				if ((*reach)[(size_t)n])
					continue;
				(*reach)[(size_t)n] = 1;
				stack.push_back(n);
			}
		}
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				char c = doc_get(doc, x, y);
				if (cell_is_thing(c) && !(*reach)[(size_t)(y * w + x)])
					add(problems, x, y, false, "%s cannot be reached from the start",
						c == 'E' ? "Guard" : c == '+' ? "Medkit" : c == 'a' ? "Ammo"
						: c == 'b' ? "Barrel" : "Lamp");
			}
		}
	}
	if (stats->enemies == 0)
		add(problems, -1, -1, false, "No guards on the map");

	/* Errors first, then warnings, each in reading order. */
	std::vector<Problem> sorted;
	sorted.reserve(problems->size());
	for (int pass = 0; pass < 2; pass++) {
		for (const Problem &p : *problems) {
			if (p.error == (pass == 0))
				sorted.push_back(p);
		}
	}
	problems->swap(sorted);
	for (const Problem &p : *problems) {
		if (p.error)
			stats->errors++;
		else
			stats->warnings++;
	}
}
