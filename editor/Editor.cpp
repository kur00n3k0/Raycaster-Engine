/*
 * Map editor: a top-down 2D view of a map file, Dear ImGui (docking branch)
 * for the UI. Walls, doors, items and guards are painted onto the grid with
 * a few tools, problems the game would reject are shown live, and F5 saves
 * and starts the game on the map.
 *
 *     ./build/map_editor [assets/maps/e1m1.txt]
 *
 * Run it from the repo root: it loads the game's textures and sprites from
 * assets/ for the palette and the map view, and previews each map's music
 * (@music) through the game's audio code with the SoundFont from raycaster.cfg. The game itself never links ImGui.
 */

#include "MapDoc.h"

#include "Audio.h"
#include "Config.h"
#include "Map.h"
#include "Palette.h"
#include "Textures.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"	/* DockBuilder, for the default layout */
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

extern char **environ;

static const char *MAP_DIR = "assets/maps";
static const char *CONFIG_FILE = "raycaster.cfg";	/* the game's: SoundFont and music volume */
static const float PREVIEW_GAIN = 0.35f;		/* MUSIC_GAIN in main.cpp */
static const char *INI_FILE = "map_editor.ini";
static const float ZOOM_MIN = 4.0f;
static const float ZOOM_MAX = 96.0f;
static const float BRUSH_ICON = 40.0f;

/* --------------------------------------------------------------------------
 * Brushes and tools
 * ------------------------------------------------------------------------ */

enum Tool {
	TOOL_PENCIL,
	TOOL_LINE,
	TOOL_RECT,	/* outline: draws a room in one drag */
	TOOL_FILLED,
	TOOL_FILL,	/* flood fill */
	TOOL_PICK,
	TOOL_COUNT
};

struct ToolInfo {
	const char *name;
	char key;
	const char *help;
};

static const ToolInfo TOOLS[TOOL_COUNT] = {
	{ "Pencil", 'q', "Paint cells by clicking or dragging" },
	{ "Line", 'w', "Drag a straight line" },
	{ "Room", 'r', "Drag a rectangle outline: four walls at once" },
	{ "Box", 'f', "Drag a filled rectangle" },
	{ "Fill", 'g', "Flood fill the area of identical cells" },
	{ "Pick", 'i', "Take the brush from a cell (also Alt+click)" },
};

enum BrushKind { BRUSH_WALL, BRUSH_DOOR, BRUSH_SECRET, BRUSH_FLOOR, BRUSH_PLAYER, BRUSH_THING };

struct Brush {
	char cell;		/* map character */
	const char *name;
	char key;		/* typed character that selects it */
	BrushKind kind;
	int image;		/* wall tile or SpriteId, see brush_texture */
};

static const Brush BRUSHES[] = {
	{ '#', "Wall 1", '1', BRUSH_WALL, 1 },
	{ '2', "Wall 2", '2', BRUSH_WALL, 2 },
	{ '3', "Wall 3", '3', BRUSH_WALL, 3 },
	{ '4', "Wall 4", '4', BRUSH_WALL, 4 },
	{ '5', "Wall 5", '5', BRUSH_WALL, 5 },
	{ '6', "Wall 6", '6', BRUSH_WALL, 6 },
	{ '7', "Wall 7", '7', BRUSH_WALL, 7 },
	{ '8', "Wall 8", '8', BRUSH_WALL, 8 },
	{ '9', "Wall 9", '9', BRUSH_WALL, 9 },
	{ 'D', "Door", 'd', BRUSH_DOOR, TILE_DOOR },
	{ 'S', "Secret wall", 's', BRUSH_SECRET, 1 },
	{ '.', "Floor (eraser)", '.', BRUSH_FLOOR, 0 },
	{ 'P', "Player start", 'p', BRUSH_PLAYER, 0 },
	{ 'E', "Guard", 'e', BRUSH_THING, SPR_ENEMY_STAND },
	{ '+', "Medkit", '+', BRUSH_THING, SPR_HEALTH },
	{ 'a', "Ammo", 'a', BRUSH_THING, SPR_AMMO },
	{ 'b', "Barrel", 'b', BRUSH_THING, SPR_BARREL },
	{ 'l', "Lamp", 'l', BRUSH_THING, SPR_LAMP },
};
enum { BRUSH_COUNT = sizeof(BRUSHES) / sizeof(BRUSHES[0]) };

static int brush_for_cell(char c)
{
	if (c == '1')
		c = '#';
	for (int i = 0; i < BRUSH_COUNT; i++) {
		if (BRUSHES[i].cell == c)
			return i;
	}
	return -1;
}

/* --------------------------------------------------------------------------
 * Textures: the game's PCX art converted to RGBA GL textures
 * ------------------------------------------------------------------------ */

struct GLImage {
	GLuint tex;
	ImU32 average;		/* mean opaque colour, for the flat view and small zoom */
};

struct Art {
	bool loaded;
	GLImage wall[TILE_DOOR + 1];	/* 1-9, TILE_DOOR */
	GLImage floor;
	GLImage sprite[SPR_COUNT];
};

static GLImage upload(const Texture *t, const Palette *pal)
{
	std::vector<uint32_t> rgba((size_t)t->width * (size_t)t->height);
	unsigned sum[3] = { 0, 0, 0 }, opaque = 0;
	for (int y = 0; y < t->height; y++) {
		for (int x = 0; x < t->width; x++) {
			uint8_t i = t->columns[x * t->height + y];	/* column-major */
			const uint8_t *c = &pal->rgb[i * 3];
			uint32_t a = i == PAL_TRANSPARENT ? 0 : 255;
			rgba[(size_t)(y * t->width + x)] = (uint32_t)c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16 | a << 24;
			if (a) {
				sum[0] += c[0];
				sum[1] += c[1];
				sum[2] += c[2];
				opaque++;
			}
		}
	}
	GLImage img;
	glGenTextures(1, &img.tex);
	glBindTexture(GL_TEXTURE_2D, img.tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	if (!opaque)
		opaque = 1;
	img.average = IM_COL32(sum[0] / opaque, sum[1] / opaque, sum[2] / opaque, 255);
	return img;
}

static bool art_load(Art *art)
{
	static Palette pal;
	static WallTextures walls;
	static FlatTextures flats;
	static SpriteTextures sprites;
	palette_build_default(&pal);
	if (!textures_load_walls(&walls, &pal) || !textures_load_flats(&flats, &pal)
		|| !textures_load_sprites(&sprites, &pal))
		return false;
	for (int t = TILE_WALL_FIRST; t <= TILE_DOOR; t++)
		art->wall[t] = upload(&walls.tile[t], &pal);
	art->floor = upload(&flats.floor, &pal);
	for (int s = 0; s < SPR_COUNT; s++)
		art->sprite[s] = upload(&sprites.sprite[s], &pal);
	textures_free_walls(&walls);
	textures_free_flats(&flats);
	textures_free_sprites(&sprites);
	art->loaded = true;
	return true;
}

static ImTextureRef tex_ref(const GLImage &img)
{
	return ImTextureRef((ImTextureID)(intptr_t)img.tex);
}

/* --------------------------------------------------------------------------
 * Editor state
 * ------------------------------------------------------------------------ */

enum Pending { PENDING_NONE, PENDING_NEW, PENDING_OPEN, PENDING_QUIT };

struct Stroke {
	bool active;
	bool erase;		/* right button: paints floor */
	int startX, startY;
	int lastX, lastY;
};

struct Editor {
	MapDoc doc;
	Art art;
	int tool;
	int brush;

	/* View: cell (x, y) is drawn at canvas origin + pan + (x, y) * zoom. */
	float zoom;
	ImVec2 pan;
	bool fitRequested;
	bool viewTouched;	/* user zoomed or panned: stop re-fitting on resize */
	ImVec2 canvasSize;
	bool showGrid;
	bool showTextures;
	bool showReach;
	int focusX, focusY;	/* center the view on this cell next frame, -1 = no */

	Stroke stroke;
	int hoverX, hoverY;
	bool hoverValid;

	std::vector<Problem> problems;
	DocStats stats;
	std::vector<uint8_t> reach;
	uint32_t validatedRevision;

	/* Dialogs */
	Pending pending;		/* action waiting on "discard changes?" */
	bool openNewPopup, openOpenPopup, openSaveAsPopup, openDiscardPopup, openErrorPopup;
	bool openResizePopup;
	char pathInput[512];
	int newWidth, newHeight;
	int resizeWidth, resizeHeight, resizeAnchor;	/* anchor 0-8, 4 = centre */
	char message[512];		/* error popup text */
	char status[256];		/* last action, shown in the status line */
	bool quit;
	bool resetLayout;

	/* Music preview, through the game's own audio path */
	Audio audio;
	Config config;
	bool previewing;

	/* Play testing */
	pid_t gamePid;
	char gameExe[PATH_MAX];
	bool afterSaveTest;		/* Save As was opened by "Test" */
	int testX, testY;		/* -1 = from the player start */
};

static void set_status(Editor *ed, const char *fmt, ...) IM_FMTARGS(2);
static void set_status(Editor *ed, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(ed->status, sizeof(ed->status), fmt, ap);
	va_end(ap);
}

static void stop_preview(Editor *ed);

static void show_error(Editor *ed, const char *text)
{
	snprintf(ed->message, sizeof(ed->message), "%s", text);
	ed->openErrorPopup = true;
}

static const char *doc_name(const MapDoc *doc)
{
	if (!doc->path[0])
		return "untitled";
	const char *slash = strrchr(doc->path, '/');
	return slash ? slash + 1 : doc->path;
}

/* --------------------------------------------------------------------------
 * File actions
 * ------------------------------------------------------------------------ */

static void do_open(Editor *ed, const char *path)
{
	char err[256];
	MapDoc loaded;
	loaded.revision = ed->doc.revision;
	if (!doc_load(&loaded, path, err, sizeof(err))) {
		show_error(ed, err);
		return;
	}
	ed->doc = loaded;
	ed->fitRequested = true;
	ed->stroke.active = false;
	set_status(ed, "Opened %s", path);
	if (err[0])
		show_error(ed, err);
}

static bool do_save(Editor *ed, const char *path)
{
	if (!doc_save(&ed->doc, path)) {
		char text[600];
		snprintf(text, sizeof(text), "Cannot write %s", path);
		show_error(ed, text);
		return false;
	}
	set_status(ed, "Saved %s", path);
	return true;
}

/* Save to the current path, or ask for one. False if nothing was written yet. */
static bool save_or_ask(Editor *ed)
{
	if (ed->doc.path[0])
		return do_save(ed, ed->doc.path);
	snprintf(ed->pathInput, sizeof(ed->pathInput), "%s/untitled.txt", MAP_DIR);
	ed->openSaveAsPopup = true;
	return false;
}

static void request(Editor *ed, Pending action)
{
	if (ed->doc.dirty) {
		ed->pending = action;
		ed->openDiscardPopup = true;
		return;
	}
	switch (action) {
	case PENDING_NEW:
		ed->openNewPopup = true;
		break;
	case PENDING_OPEN:
		snprintf(ed->pathInput, sizeof(ed->pathInput), "%s", ed->doc.path[0] ? ed->doc.path : MAP_DIR);
		ed->openOpenPopup = true;
		break;
	case PENDING_QUIT:
		ed->quit = true;
		break;
	case PENDING_NONE:
		break;
	}
}

/* --------------------------------------------------------------------------
 * Play testing: save, then run ./raycaster -map <file> next to this binary
 * ------------------------------------------------------------------------ */

static void find_game(Editor *ed)
{
	char self[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
	ed->gameExe[0] = '\0';
	if (n <= 0)
		return;
	self[n] = '\0';
	char *slash = strrchr(self, '/');
	if (!slash)
		return;
	*slash = '\0';
	snprintf(ed->gameExe, sizeof(ed->gameExe), "%s/raycaster", self);
}

static void launch_game(Editor *ed)
{
	if (ed->gamePid > 0) {
		set_status(ed, "The game is already running");
		return;
	}
	if (access(ed->gameExe, X_OK) != 0) {
		char text[PATH_MAX + 64];
		snprintf(text, sizeof(text), "Cannot run %s: build the raycaster target first.", ed->gameExe);
		show_error(ed, text);
		return;
	}

	char warp[3][32];
	const char *argv[10];
	int argc = 0;
	argv[argc++] = ed->gameExe;
	argv[argc++] = "-map";
	argv[argc++] = ed->doc.path;
	if (ed->testX >= 0) {
		snprintf(warp[0], sizeof(warp[0]), "%.1f", ed->testX + 0.5);
		snprintf(warp[1], sizeof(warp[1]), "%.1f", ed->testY + 0.5);
		snprintf(warp[2], sizeof(warp[2]), "0");
		argv[argc++] = "-warp";
		argv[argc++] = warp[0];
		argv[argc++] = warp[1];
		argv[argc++] = warp[2];
	}
	argv[argc] = nullptr;

	pid_t pid;
	if (posix_spawn(&pid, ed->gameExe, nullptr, nullptr, (char *const *)argv, environ) != 0) {
		show_error(ed, "Could not start the game");
		return;
	}
	ed->gamePid = pid;
	stop_preview(ed);	/* the game plays the map's music itself */
	set_status(ed, "Testing %s in the game", doc_name(&ed->doc));
}

/* x, y = start cell, or -1 for the map's player start. */
static void test_map(Editor *ed, int x, int y)
{
	if (ed->stats.errors > 0) {
		char text[256];
		snprintf(text, sizeof(text), "The map has %d error(s) the game would reject.\n"
			"See the Problems window.", ed->stats.errors);
		show_error(ed, text);
		return;
	}
	if (x >= 0 && cell_is_solid(doc_get(&ed->doc, x, y))) {
		show_error(ed, "Cannot start inside a wall.");
		return;
	}
	ed->testX = x;
	ed->testY = y;
	if (!ed->doc.dirty && ed->doc.path[0]) {
		launch_game(ed);
		return;
	}
	if (save_or_ask(ed))
		launch_game(ed);
	else
		ed->afterSaveTest = !ed->doc.path[0];
}

static void reap_game(Editor *ed)
{
	if (ed->gamePid <= 0)
		return;
	int status;
	if (waitpid(ed->gamePid, &status, WNOHANG) == ed->gamePid) {
		ed->gamePid = 0;
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
			set_status(ed, "The game exited with an error (see the terminal)");
		else
			set_status(ed, "Back from the game");
	}
}

/* --------------------------------------------------------------------------
 * Editing
 * ------------------------------------------------------------------------ */

static char stroke_cell(const Editor *ed)
{
	return ed->stroke.erase ? '.' : BRUSHES[ed->brush].cell;
}

/* Calls fn(x, y) for each cell of the line, Bresenham. */
template <typename F>
static void for_line(int x0, int y0, int x1, int y1, F fn)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		fn(x0, y0);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/* Cells covered by the line / rectangle tools between the stroke start and (x, y). */
template <typename F>
static void for_shape(int tool, int x0, int y0, int x1, int y1, F fn)
{
	if (tool == TOOL_LINE) {
		for_line(x0, y0, x1, y1, fn);
		return;
	}
	int ax = std::min(x0, x1), bx = std::max(x0, x1);
	int ay = std::min(y0, y1), by = std::max(y0, y1);
	for (int y = ay; y <= by; y++) {
		for (int x = ax; x <= bx; x++) {
			if (tool == TOOL_FILLED || x == ax || x == bx || y == ay || y == by)
				fn(x, y);
		}
	}
}

static void pick(Editor *ed, int x, int y)
{
	int b = brush_for_cell(doc_get(&ed->doc, x, y));
	if (b >= 0)
		ed->brush = b;
}

static void stroke_begin(Editor *ed, int x, int y, bool erase, bool alt)
{
	if (alt || ed->tool == TOOL_PICK) {
		pick(ed, x, y);
		return;
	}
	Stroke *s = &ed->stroke;
	s->active = true;
	s->erase = erase;
	s->startX = s->lastX = x;
	s->startY = s->lastY = y;
	doc_begin_edit(&ed->doc);
	if (ed->tool == TOOL_PENCIL)
		doc_set(&ed->doc, x, y, stroke_cell(ed));
	else if (ed->tool == TOOL_FILL)
		doc_flood_fill(&ed->doc, x, y, stroke_cell(ed));
}

static void stroke_move(Editor *ed, int x, int y)
{
	Stroke *s = &ed->stroke;
	if (ed->tool == TOOL_PENCIL && (x != s->lastX || y != s->lastY)) {
		char c = stroke_cell(ed);
		MapDoc *doc = &ed->doc;
		/* A line from the last cell, so fast drags leave no gaps. */
		for_line(s->lastX, s->lastY, x, y, [doc, c](int cx, int cy) { doc_set(doc, cx, cy, c); });
	}
	s->lastX = x;
	s->lastY = y;
}

static void stroke_end(Editor *ed)
{
	Stroke *s = &ed->stroke;
	if (!s->active)
		return;
	if (ed->tool == TOOL_LINE || ed->tool == TOOL_RECT || ed->tool == TOOL_FILLED) {
		char c = stroke_cell(ed);
		MapDoc *doc = &ed->doc;
		for_shape(ed->tool, s->startX, s->startY, s->lastX, s->lastY,
			[doc, c](int x, int y) { doc_set(doc, x, y, c); });
	}
	doc_end_edit(&ed->doc);
	s->active = false;
}

static void undo(Editor *ed)
{
	ed->stroke.active = false;
	if (doc_undo(&ed->doc))
		set_status(ed, "Undo");
}

static void redo(Editor *ed)
{
	ed->stroke.active = false;
	if (doc_redo(&ed->doc))
		set_status(ed, "Redo");
}

/* --------------------------------------------------------------------------
 * Map view
 * ------------------------------------------------------------------------ */

static void fit_view(Editor *ed, ImVec2 size)
{
	float zx = (size.x - 16.0f) / (float)ed->doc.width;
	float zy = (size.y - 16.0f) / (float)ed->doc.height;
	ed->zoom = std::max(ZOOM_MIN, std::min(ZOOM_MAX, std::min(zx, zy)));
	ed->pan.x = floorf((size.x - ed->zoom * (float)ed->doc.width) * 0.5f);
	ed->pan.y = floorf((size.y - ed->zoom * (float)ed->doc.height) * 0.5f);
}

static const ImU32 FLOOR_COLOR = IM_COL32(44, 44, 50, 255);
static const ImU32 VOID_COLOR = IM_COL32(24, 24, 28, 255);

/* One cell. alpha < 1 draws a ghost (tool preview). */
static void draw_cell(ImDrawList *dl, const Editor *ed, char c, int x, int y, ImVec2 a, ImVec2 b, float alpha)
{
	const Art *art = &ed->art;
	const float z = ed->zoom;
	ImU32 tint = IM_COL32(255, 255, 255, (int)(alpha * 255.0f));
	ImU32 floorCol = alpha < 1.0f ? IM_COL32(44, 44, 50, (int)(alpha * 255.0f)) : FLOOR_COLOR;

	if (cell_is_wall(c)) {
		int tile = c == '#' ? 1 : c - '0';
		if (ed->showTextures && z >= 8.0f)
			dl->AddImage(tex_ref(art->wall[tile]), a, b, ImVec2(0, 0), ImVec2(1, 1), tint);
		else
			dl->AddRectFilled(a, b, (art->wall[tile].average & ~IM_COL32_A_MASK) | (tint & IM_COL32_A_MASK));
		return;
	}

	dl->AddRectFilled(a, b, floorCol);
	switch (c) {
	case 'S': {
		if (ed->showTextures && z >= 8.0f)
			dl->AddImage(tex_ref(art->wall[1]), a, b, ImVec2(0, 0), ImVec2(1, 1), tint);
		else
			dl->AddRectFilled(a, b, (art->wall[1].average & ~IM_COL32_A_MASK) | (tint & IM_COL32_A_MASK));
		ImU32 mark = IM_COL32(255, 220, 60, (int)(alpha * 255.0f));
		float t = std::max(1.0f, z * 0.08f);
		dl->AddRect(ImVec2(a.x + t, a.y + t), ImVec2(b.x - t, b.y - t), mark, 0.0f, 0, t);
		if (z >= 14.0f) {
			ImVec2 ts = ImGui::CalcTextSize("S");
			dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), mark, "S");
		}
		break;
	}
	case 'D': {
		bool vertical;
		bool ok = doc_door_vertical(&ed->doc, x, y, &vertical);
		const GLImage &img = art->wall[TILE_DOOR];
		ImVec2 pa = a, pb = b;
		if (ok) {
			/* The panel sits on the cell's mid-plane, across the passage. */
			float half = std::max(1.5f, z * 0.14f);
			if (vertical) {
				pa.x = (a.x + b.x) * 0.5f - half;
				pb.x = (a.x + b.x) * 0.5f + half;
			} else {
				pa.y = (a.y + b.y) * 0.5f - half;
				pb.y = (a.y + b.y) * 0.5f + half;
			}
		}
		if (ed->showTextures && z >= 8.0f)
			dl->AddImage(tex_ref(img), pa, pb, ImVec2(0, 0), ImVec2(1, 1), tint);
		else
			dl->AddRectFilled(pa, pb, (img.average & ~IM_COL32_A_MASK) | (tint & IM_COL32_A_MASK));
		if (!ok) {
			ImU32 red = IM_COL32(255, 60, 60, (int)(alpha * 255.0f));
			dl->AddLine(a, b, red, 2.0f);
			dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), red, 2.0f);
		}
		break;
	}
	case 'P': {
		/* Green arrow: the player always starts facing east. */
		ImU32 green = IM_COL32(80, 230, 100, (int)(alpha * 255.0f));
		float m = z * 0.18f;
		dl->AddTriangleFilled(ImVec2(a.x + m, a.y + m), ImVec2(b.x - m, (a.y + b.y) * 0.5f),
			ImVec2(a.x + m, b.y - m), green);
		break;
	}
	default: {
		int brush = brush_for_cell(c);
		if (brush >= 0 && BRUSHES[brush].kind == BRUSH_THING) {
			const GLImage &img = art->sprite[BRUSHES[brush].image];
			if (ed->showTextures && z >= 8.0f) {
				dl->AddImage(tex_ref(img), a, b, ImVec2(0, 0), ImVec2(1, 1), tint);
			} else {
				float r = z * 0.32f;
				dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), r,
					(img.average & ~IM_COL32_A_MASK) | (tint & IM_COL32_A_MASK));
			}
		}
		break;
	}
	}
}

static void handle_view_input(Editor *ed, ImVec2 origin, bool hovered, bool active)
{
	ImGuiIO &io = ImGui::GetIO();

	/* Zoom around the mouse. */
	if (hovered && io.MouseWheel != 0.0f) {
		float old = ed->zoom;
		float zoom = old * (io.MouseWheel > 0.0f ? 1.2f : 1.0f / 1.2f);
		zoom = std::max(ZOOM_MIN, std::min(ZOOM_MAX, zoom));
		ImVec2 m = ImVec2(io.MousePos.x - origin.x - ed->pan.x, io.MousePos.y - origin.y - ed->pan.y);
		ed->pan.x -= m.x * (zoom / old - 1.0f);
		ed->pan.y -= m.y * (zoom / old - 1.0f);
		ed->zoom = zoom;
		ed->viewTouched = true;
	}

	/* Pan with the middle button, or Space + left button. */
	bool spacePan = ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || spacePan)) {
		ed->pan.x += io.MouseDelta.x;
		ed->pan.y += io.MouseDelta.y;
		ed->viewTouched = true;
	}
}

static void handle_paint_input(Editor *ed, bool hovered)
{
	ImGuiIO &io = ImGui::GetIO();
	if (ImGui::IsKeyDown(ImGuiKey_Space))
		return;
	int x = ed->hoverX, y = ed->hoverY;

	if (!ed->stroke.active && hovered && ed->hoverValid) {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			stroke_begin(ed, x, y, false, io.KeyAlt);
		else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			stroke_begin(ed, x, y, true, false);
	}
	if (ed->stroke.active) {
		/* Shapes may be dragged past the edge; they are clipped when committed. */
		stroke_move(ed, x, y);
		ImGuiMouseButton button = ed->stroke.erase ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
		if (!ImGui::IsMouseDown(button))
			stroke_end(ed);
	}
}

static void draw_map_window(Editor *ed)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	bool open = ImGui::Begin("Map", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	if (!open) {
		ImGui::End();
		return;
	}

	const float statusH = ImGui::GetFrameHeightWithSpacing();
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();
	size.y -= statusH;
	size.x = std::max(size.x, 64.0f);
	size.y = std::max(size.y, 64.0f);

	/* Fit on request, and keep fitting while the docked window settles until the user moves the view. */
	bool resized = size.x != ed->canvasSize.x || size.y != ed->canvasSize.y;
	ed->canvasSize = size;
	if (ed->fitRequested || (resized && !ed->viewTouched)) {
		fit_view(ed, size);
		ed->fitRequested = false;
		ed->viewTouched = false;
	}
	if (ed->focusX >= 0) {
		ed->pan.x = floorf(size.x * 0.5f - ((float)ed->focusX + 0.5f) * ed->zoom);
		ed->pan.y = floorf(size.y * 0.5f - ((float)ed->focusY + 0.5f) * ed->zoom);
		ed->focusX = ed->focusY = -1;
		ed->viewTouched = true;
	}

	ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft
		| ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	bool hovered = ImGui::IsItemHovered();
	bool active = ImGui::IsItemActive();
	if (hovered)
		ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);	/* keep the wheel from scrolling the window */

	handle_view_input(ed, origin, hovered, active);

	ImGuiIO &io = ImGui::GetIO();
	const float z = ed->zoom;
	ImVec2 base = ImVec2(origin.x + ed->pan.x, origin.y + ed->pan.y);
	ed->hoverX = (int)floorf((io.MousePos.x - base.x) / z);
	ed->hoverY = (int)floorf((io.MousePos.y - base.y) / z);
	ed->hoverValid = doc_inside(&ed->doc, ed->hoverX, ed->hoverY);
	handle_paint_input(ed, hovered);

	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 end = ImVec2(origin.x + size.x, origin.y + size.y);
	dl->PushClipRect(origin, end, true);
	dl->AddRectFilled(origin, end, VOID_COLOR);

	const MapDoc *doc = &ed->doc;
	int x0 = std::max(0, (int)floorf((origin.x - base.x) / z));
	int y0 = std::max(0, (int)floorf((origin.y - base.y) / z));
	int x1 = std::min(doc->width - 1, (int)floorf((end.x - base.x) / z));
	int y1 = std::min(doc->height - 1, (int)floorf((end.y - base.y) / z));

	auto cell_rect = [&](int x, int y, ImVec2 *a, ImVec2 *b) {
		*a = ImVec2(floorf(base.x + (float)x * z), floorf(base.y + (float)y * z));
		*b = ImVec2(floorf(base.x + (float)(x + 1) * z), floorf(base.y + (float)(y + 1) * z));
	};

	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			ImVec2 a, b;
			cell_rect(x, y, &a, &b);
			char c = doc_get(doc, x, y);
			draw_cell(dl, ed, c, x, y, a, b, 1.0f);
			if (ed->showReach && !cell_is_wall(c) && !ed->reach.empty() && !ed->reach[(size_t)(y * doc->width + x)])
				dl->AddRectFilled(a, b, IM_COL32(200, 40, 40, 70));
		}
	}

	if (ed->showGrid && z >= 8.0f) {
		ImU32 gridCol = IM_COL32(255, 255, 255, 28);
		for (int x = x0; x <= x1 + 1; x++) {
			float px = floorf(base.x + (float)x * z);
			dl->AddLine(ImVec2(px, base.y + (float)y0 * z), ImVec2(px, base.y + (float)(y1 + 1) * z), gridCol);
		}
		for (int y = y0; y <= y1 + 1; y++) {
			float py = floorf(base.y + (float)y * z);
			dl->AddLine(ImVec2(base.x + (float)x0 * z, py), ImVec2(base.x + (float)(x1 + 1) * z, py), gridCol);
		}
	}
	dl->AddRect(ImVec2(base.x - 1, base.y - 1),
		ImVec2(base.x + (float)doc->width * z + 1, base.y + (float)doc->height * z + 1), IM_COL32(120, 120, 140, 255));

	/* Problem cells. */
	for (const Problem &p : ed->problems) {
		if (p.x < 0)
			continue;
		ImVec2 a, b;
		cell_rect(p.x, p.y, &a, &b);
		dl->AddRect(a, b, p.error ? IM_COL32(255, 60, 60, 255) : IM_COL32(255, 170, 40, 255), 0.0f, 0, 2.0f);
	}

	/* Tool preview: the shape being dragged, or the brush under the cursor. */
	const Stroke *s = &ed->stroke;
	if (s->active && (ed->tool == TOOL_LINE || ed->tool == TOOL_RECT || ed->tool == TOOL_FILLED)) {
		char c = stroke_cell(ed);
		for_shape(ed->tool, s->startX, s->startY, s->lastX, s->lastY, [&](int x, int y) {
			if (!doc_inside(doc, x, y))
				return;
			ImVec2 a, b;
			cell_rect(x, y, &a, &b);
			draw_cell(dl, ed, c, x, y, a, b, 0.75f);
			dl->AddRect(a, b, IM_COL32(255, 255, 255, 120));
		});
	} else if (hovered && ed->hoverValid && !ImGui::IsKeyDown(ImGuiKey_Space)) {
		ImVec2 a, b;
		cell_rect(ed->hoverX, ed->hoverY, &a, &b);
		if (ed->tool != TOOL_PICK && !io.KeyAlt && !s->active)
			draw_cell(dl, ed, BRUSHES[ed->brush].cell, ed->hoverX, ed->hoverY, a, b, 0.5f);
		dl->AddRect(a, b, IM_COL32(255, 255, 255, 220), 0.0f, 0, 1.5f);
	}
	dl->PopClipRect();

	/* Status line. */
	ImGui::SetCursorScreenPos(ImVec2(origin.x + 6.0f, origin.y + size.y + 3.0f));
	if (ed->hoverValid) {
		char c = doc_get(doc, ed->hoverX, ed->hoverY);
		int b = brush_for_cell(c);
		ImGui::Text("%3d,%3d  %-14s", ed->hoverX, ed->hoverY, b >= 0 ? BRUSHES[b].name : "?");
	} else {
		ImGui::Text("%-22s", "");
	}
	ImGui::SameLine();
	ImGui::TextDisabled("| %s  %s  zoom %d%%  |", TOOLS[ed->tool].name, BRUSHES[ed->brush].name,
		(int)(z / 16.0f * 100.0f + 0.5f));
	ImGui::SameLine();
	ImGui::TextUnformatted(ed->status);
	ImGui::End();
}

/* --------------------------------------------------------------------------
 * Music
 * ------------------------------------------------------------------------ */

/* .mid files the game can play, for the Song combo. */
static std::vector<std::string> list_music()
{
	std::vector<std::string> out;
	DIR *d = opendir(MAP_MUSIC_DIR);
	if (!d)
		return out;
	while (dirent *e = readdir(d)) {
		if (map_music_name_ok(e->d_name) && strcmp(e->d_name, "none") != 0)
			out.push_back(e->d_name);
	}
	closedir(d);
	std::sort(out.begin(), out.end());
	return out;
}

/* The file the game will play for this map, or null for silence. */
static const char *map_song(const MapDoc *doc)
{
	if (!doc->music[0])
		return MAP_MUSIC_DEFAULT;
	return strcmp(doc->music, "none") == 0 ? nullptr : doc->music;
}

static void stop_preview(Editor *ed)
{
	audio_stop_music(&ed->audio);
	ed->previewing = false;
}

static void start_preview(Editor *ed)
{
	stop_preview(ed);
	const char *song = map_song(&ed->doc);
	if (!song)
		return;
	if (!ed->audio.enabled) {
		set_status(ed, "No audio device: cannot preview");
		return;
	}
	char path[256];
	snprintf(path, sizeof(path), "%s%s", MAP_MUSIC_DIR, song);
	if (!audio_play_music(&ed->audio, path, ed->config.soundfont, PREVIEW_GAIN * ed->config.musicVolume)) {
		set_status(ed, "Cannot play %s (see the terminal)", path);
		return;
	}
	ed->previewing = true;
	set_status(ed, "Playing %s", song);
}

static void draw_music_settings(Editor *ed)
{
	MapDoc *doc = &ed->doc;
	char current[96];
	if (!doc->music[0])
		snprintf(current, sizeof(current), "Default (%s)", MAP_MUSIC_DEFAULT);
	else if (strcmp(doc->music, "none") == 0)
		snprintf(current, sizeof(current), "None (silence)");
	else
		snprintf(current, sizeof(current), "%s", doc->music);

	const char *picked = nullptr;
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##song", current)) {
		static std::vector<std::string> songs;
		if (ImGui::IsWindowAppearing())
			songs = list_music();
		char label[96];
		snprintf(label, sizeof(label), "Default (%s)", MAP_MUSIC_DEFAULT);
		if (ImGui::Selectable(label, !doc->music[0]))
			picked = "";
		if (ImGui::Selectable("None (silence)", strcmp(doc->music, "none") == 0))
			picked = "none";
		ImGui::Separator();
		for (const std::string &s : songs) {
			if (ImGui::Selectable(s.c_str(), s == doc->music))
				picked = s.c_str();
		}
		/* Change while `songs` is alive: picked may point into it. */
		if (picked && strcmp(picked, doc->music) != 0) {
			doc_begin_edit(doc);
			doc_set_music(doc, picked);
			doc_end_edit(doc);
			if (ed->previewing)
				start_preview(ed);
		}
		ImGui::EndCombo();
	}

	ImGui::BeginDisabled(!map_song(doc));
	if (ImGui::Button(ed->previewing ? "Stop preview" : "Preview"))
		ed->previewing ? stop_preview(ed) : start_preview(ed);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("saved as @music in the map file");
}

/* --------------------------------------------------------------------------
 * Side windows
 * ------------------------------------------------------------------------ */

static bool brush_button(Editor *ed, int i)
{
	const Brush *br = &BRUSHES[i];
	const Art *art = &ed->art;
	ImVec2 sz(BRUSH_ICON, BRUSH_ICON);
	bool selected = ed->brush == i;
	ImGui::PushID(i);
	if (selected) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
		ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 220, 60, 255));
	}
	bool clicked;
	ImVec4 bg = ImGui::ColorConvertU32ToFloat4(FLOOR_COLOR);
	switch (br->kind) {
	case BRUSH_WALL:
	case BRUSH_SECRET:
		clicked = ImGui::ImageButton("b", tex_ref(art->wall[br->image]), sz);
		break;
	case BRUSH_DOOR:
		clicked = ImGui::ImageButton("b", tex_ref(art->wall[TILE_DOOR]), sz);
		break;
	case BRUSH_THING:
		clicked = ImGui::ImageButton("b", tex_ref(art->sprite[br->image]), sz, ImVec2(0, 0), ImVec2(1, 1), bg);
		break;
	default: {
		/* Floor and player: drawn by hand like in the map view. */
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImVec2 pad = ImGui::GetStyle().FramePadding;
		clicked = ImGui::Button("##b", ImVec2(sz.x + pad.x * 2.0f, sz.y + pad.y * 2.0f));
		ImVec2 a = ImVec2(p.x + pad.x, p.y + pad.y), b = ImVec2(a.x + sz.x, a.y + sz.y);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		float savedZoom = ed->zoom;
		ed->zoom = BRUSH_ICON;
		draw_cell(dl, ed, br->cell, -10, -10, a, b, 1.0f);
		ed->zoom = savedZoom;
		break;
	}
	}
	if (br->kind == BRUSH_SECRET) {
		ImVec2 b = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddText(ImVec2(b.x - 14.0f, b.y - 18.0f), IM_COL32(255, 220, 60, 255), "S");
	}
	if (selected) {
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s\nkey: %c   map char: %c", br->name, br->key == '#' ? '1' : br->key, br->cell);
	ImGui::PopID();
	if (clicked)
		ed->brush = i;
	return clicked;
}

static void draw_palette_window(Editor *ed)
{
	if (!ImGui::Begin("Palette")) {
		ImGui::End();
		return;
	}
	ImGui::SeparatorText("Tools");
	if (ImGui::BeginTable("tools", 2)) {
		for (int t = 0; t < TOOL_COUNT; t++) {
			char label[32];
			snprintf(label, sizeof(label), "%s (%c)", TOOLS[t].name, toupper(TOOLS[t].key));
			ImGui::TableNextColumn();
			if (ImGui::RadioButton(label, ed->tool == t))
				ed->tool = t;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", TOOLS[t].help);
		}
		ImGui::EndTable();
	}

	float avail = ImGui::GetContentRegionAvail().x;
	float step = BRUSH_ICON + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
	int perRow = std::max(1, (int)(avail / step));
	auto section = [&](const char *title, BrushKind lo, BrushKind hi) {
		ImGui::SeparatorText(title);
		int n = 0;
		for (int i = 0; i < BRUSH_COUNT; i++) {
			if (BRUSHES[i].kind < lo || BRUSHES[i].kind > hi)
				continue;
			if (n % perRow)
				ImGui::SameLine();
			brush_button(ed, i);
			n++;
		}
	};
	section("Walls", BRUSH_WALL, BRUSH_WALL);
	section("Doors & floor", BRUSH_DOOR, BRUSH_FLOOR);
	section("Player, guards & items", BRUSH_PLAYER, BRUSH_THING);

	ImGui::Spacing();
	ImGui::TextWrapped("Left button paints, right button erases (floor). Middle button or "
		"Space + drag pans, wheel zooms. Alt + click picks a brush from the map.");
	ImGui::End();
}

static void draw_properties_window(Editor *ed)
{
	if (!ImGui::Begin("Properties")) {
		ImGui::End();
		return;
	}
	MapDoc *doc = &ed->doc;
	ImGui::Text("File: %s%s", doc->path[0] ? doc->path : "(not saved)", doc->dirty ? " *" : "");
	ImGui::Text("Size: %d x %d", doc->width, doc->height);
	if (ImGui::Button("Resize..."))
		ed->openResizePopup = true;
	ImGui::SameLine();
	if (ImGui::Button("Wall in border")) {
		doc_begin_edit(doc);
		doc_wall_border(doc);
		doc_end_edit(doc);
	}

	ImGui::SeparatorText("Music");
	draw_music_settings(ed);

	ImGui::SeparatorText("Contents");
	const DocStats *st = &ed->stats;
	if (ImGui::BeginTable("stats", 2, ImGuiTableFlags_SizingStretchProp)) {
		auto row = [](const char *name, int v) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(name);
			ImGui::TableNextColumn();
			ImGui::Text("%d", v);
		};
		row("Guards", st->enemies);
		row("Medkits", st->health);
		row("Ammo boxes", st->ammo);
		row("Barrels", st->barrels);
		row("Lamps", st->lamps);
		row("Doors", st->doors);
		row("Secret walls", st->secrets);
		ImGui::EndTable();
	}

	ImGui::SeparatorText("View");
	ImGui::Checkbox("Textures", &ed->showTextures);
	ImGui::Checkbox("Grid", &ed->showGrid);
	ImGui::Checkbox("Shade unreachable floor", &ed->showReach);
	if (ImGui::SliderFloat("Zoom", &ed->zoom, ZOOM_MIN, ZOOM_MAX, "%.0f px"))
		ed->viewTouched = true;
	if (ImGui::Button("Fit map (Home)"))
		ed->fitRequested = true;

	ImGui::SeparatorText("Test");
	ImGui::BeginDisabled(ed->gamePid > 0);
	if (ImGui::Button("Play from start (F5)"))
		test_map(ed, -1, -1);
	ImGui::EndDisabled();
	ImGui::TextDisabled("F6: play from the cell under the mouse");
	if (ed->gamePid > 0)
		ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Game running (pid %d)", (int)ed->gamePid);
	ImGui::End();
}

static void draw_problems_window(Editor *ed)
{
	char title[64];
	snprintf(title, sizeof(title), "Problems (%d)###Problems", (int)ed->problems.size());
	if (!ImGui::Begin(title)) {
		ImGui::End();
		return;
	}
	if (ed->problems.empty())
		ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "No problems: the game can load this map.");
	int shown = 0;
	for (size_t i = 0; i < ed->problems.size() && shown < 500; i++, shown++) {
		const Problem &p = ed->problems[i];
		ImGui::PushID((int)i);
		ImVec4 col = p.error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(1.0f, 0.7f, 0.3f, 1.0f);
		char label[160];
		if (p.x >= 0)
			snprintf(label, sizeof(label), "%s  %3d,%3d  %s", p.error ? "error  " : "warning", p.x, p.y, p.text);
		else
			snprintf(label, sizeof(label), "%s  map      %s", p.error ? "error  " : "warning", p.text);
		ImGui::PushStyleColor(ImGuiCol_Text, col);
		if (ImGui::Selectable(label) && p.x >= 0) {
			ed->focusX = p.x;
			ed->focusY = p.y;
		}
		ImGui::PopStyleColor();
		ImGui::PopID();
	}
	if (ed->problems.size() > 500)
		ImGui::TextDisabled("... and %d more", (int)ed->problems.size() - 500);
	ImGui::End();
}

/* --------------------------------------------------------------------------
 * Menus, shortcuts and dialogs
 * ------------------------------------------------------------------------ */

static void draw_menu(Editor *ed)
{
	if (!ImGui::BeginMainMenuBar())
		return;
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("New...", "Ctrl+N"))
			request(ed, PENDING_NEW);
		if (ImGui::MenuItem("Open...", "Ctrl+O"))
			request(ed, PENDING_OPEN);
		if (ImGui::MenuItem("Save", "Ctrl+S"))
			save_or_ask(ed);
		if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
			snprintf(ed->pathInput, sizeof(ed->pathInput), "%s", ed->doc.path[0] ? ed->doc.path : "assets/maps/untitled.txt");
			ed->openSaveAsPopup = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Quit", "Ctrl+Q"))
			request(ed, PENDING_QUIT);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !ed->doc.undo.empty()))
			undo(ed);
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !ed->doc.redo.empty()))
			redo(ed);
		ImGui::Separator();
		if (ImGui::MenuItem("Resize map..."))
			ed->openResizePopup = true;
		if (ImGui::MenuItem("Wall in border")) {
			doc_begin_edit(&ed->doc);
			doc_wall_border(&ed->doc);
			doc_end_edit(&ed->doc);
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("View")) {
		if (ImGui::MenuItem("Fit map", "Home"))
			ed->fitRequested = true;
		ImGui::MenuItem("Textures", "T", &ed->showTextures);
		ImGui::MenuItem("Grid", "H", &ed->showGrid);
		ImGui::MenuItem("Shade unreachable floor", nullptr, &ed->showReach);
		ImGui::Separator();
		if (ImGui::MenuItem("Reset window layout"))
			ed->resetLayout = true;
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Test")) {
		if (ImGui::MenuItem("Play from start", "F5", false, ed->gamePid <= 0))
			test_map(ed, -1, -1);
		ImGui::MenuItem("Play from cell under mouse", "F6", false, false);
		ImGui::EndMenu();
	}
	char title[600];
	snprintf(title, sizeof(title), "%s%s", doc_name(&ed->doc), ed->doc.dirty ? " *" : "");
	float w = ImGui::CalcTextSize(title).x;
	ImGui::SameLine(ImGui::GetWindowWidth() - w - 16.0f);
	ImGui::TextDisabled("%s", title);
	ImGui::EndMainMenuBar();
}

static void handle_shortcuts(Editor *ed)
{
	ImGuiIO &io = ImGui::GetIO();
	const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, global))
		request(ed, PENDING_NEW);
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, global))
		request(ed, PENDING_OPEN);
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, global))
		save_or_ask(ed);
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, global)) {
		snprintf(ed->pathInput, sizeof(ed->pathInput), "%s", ed->doc.path[0] ? ed->doc.path : "assets/maps/untitled.txt");
		ed->openSaveAsPopup = true;
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, global))
		request(ed, PENDING_QUIT);
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, global | ImGuiInputFlags_Repeat))
		undo(ed);
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, global | ImGuiInputFlags_Repeat)
		|| ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, global | ImGuiInputFlags_Repeat))
		redo(ed);
	if (ImGui::Shortcut(ImGuiKey_F5, global))
		test_map(ed, -1, -1);
	if (ImGui::Shortcut(ImGuiKey_F6, global) && ed->hoverValid)
		test_map(ed, ed->hoverX, ed->hoverY);

	/* Single keys pick tools and brushes; not while typing in a text field or with a popup open. */
	if (io.WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || io.KeyCtrl || io.KeyAlt)
		return;
	if (ImGui::IsKeyPressed(ImGuiKey_Home))
		ed->fitRequested = true;
	for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
		ImWchar ch = io.InputQueueCharacters[i];
		if (ch > 127)
			continue;
		char c = (char)ch;
		if (c == '#')
			c = '1';
		if (c == '=')
			c = '+';	/* + without Shift */
		char lower = (char)tolower(c);
		if (lower == 't') {
			ed->showTextures = !ed->showTextures;
			continue;
		}
		if (lower == 'h') {
			ed->showGrid = !ed->showGrid;
			continue;
		}
		for (int t = 0; t < TOOL_COUNT; t++) {
			if (TOOLS[t].key == lower)
				ed->tool = t;
		}
		for (int b = 0; b < BRUSH_COUNT; b++) {
			if (BRUSHES[b].key == lower)
				ed->brush = b;
		}
	}
}

/* Files in MAP_DIR, for the Open dialog. */
static std::vector<std::string> list_maps()
{
	std::vector<std::string> out;
	DIR *d = opendir(MAP_DIR);
	if (!d)
		return out;
	while (dirent *e = readdir(d)) {
		size_t n = strlen(e->d_name);
		if (n > 4 && strcmp(e->d_name + n - 4, ".txt") == 0)
			out.push_back(std::string(MAP_DIR) + "/" + e->d_name);
	}
	closedir(d);
	std::sort(out.begin(), out.end());
	return out;
}

static void draw_dialogs(Editor *ed)
{
	/* OpenPopup must be called from the same ID stack level as BeginPopupModal. */
	if (ed->openNewPopup)		ImGui::OpenPopup("New map");
	if (ed->openOpenPopup)		ImGui::OpenPopup("Open map");
	if (ed->openSaveAsPopup)	ImGui::OpenPopup("Save map as");
	if (ed->openDiscardPopup)	ImGui::OpenPopup("Unsaved changes");
	if (ed->openResizePopup) {
		ed->resizeWidth = ed->doc.width;
		ed->resizeHeight = ed->doc.height;
		ed->resizeAnchor = 0;
		ImGui::OpenPopup("Resize map");
	}
	if (ed->openErrorPopup)		ImGui::OpenPopup("Message");
	ed->openNewPopup = ed->openOpenPopup = ed->openSaveAsPopup = false;
	ed->openDiscardPopup = ed->openResizePopup = ed->openErrorPopup = false;

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	const ImGuiWindowFlags modal = ImGuiWindowFlags_AlwaysAutoResize;

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("New map", nullptr, modal)) {
		ImGui::InputInt("Width", &ed->newWidth);
		ImGui::InputInt("Height", &ed->newHeight);
		ed->newWidth = std::max((int)DOC_MIN_SIZE, std::min((int)DOC_MAX_SIZE, ed->newWidth));
		ed->newHeight = std::max((int)DOC_MIN_SIZE, std::min((int)DOC_MAX_SIZE, ed->newHeight));
		ImGui::TextDisabled("Starts as one walled room with the player start.");
		if (ImGui::Button("Create", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
			doc_new(&ed->doc, ed->newWidth, ed->newHeight);
			ed->fitRequested = true;
			set_status(ed, "New %dx%d map", ed->newWidth, ed->newHeight);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Open map", nullptr, modal)) {
		static std::vector<std::string> maps;
		if (ImGui::IsWindowAppearing())
			maps = list_maps();
		ImGui::TextUnformatted(MAP_DIR);
		if (ImGui::BeginListBox("##maps", ImVec2(420, 180))) {
			for (const std::string &m : maps) {
				bool sel = m == ed->pathInput;
				if (ImGui::Selectable(m.c_str() + strlen(MAP_DIR) + 1, sel, ImGuiSelectableFlags_AllowDoubleClick)) {
					snprintf(ed->pathInput, sizeof(ed->pathInput), "%s", m.c_str());
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						do_open(ed, ed->pathInput);
						ImGui::CloseCurrentPopup();
					}
				}
			}
			ImGui::EndListBox();
		}
		ImGui::SetNextItemWidth(420);
		bool enter = ImGui::InputText("##path", ed->pathInput, sizeof(ed->pathInput), ImGuiInputTextFlags_EnterReturnsTrue);
		if (ImGui::Button("Open", ImVec2(120, 0)) || enter) {
			do_open(ed, ed->pathInput);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Save map as", nullptr, modal)) {
		ImGui::SetNextItemWidth(420);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		bool enter = ImGui::InputText("##path", ed->pathInput, sizeof(ed->pathInput), ImGuiInputTextFlags_EnterReturnsTrue);
		struct stat st;
		bool exists = stat(ed->pathInput, &st) == 0 && strcmp(ed->pathInput, ed->doc.path) != 0;
		if (exists)
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "This file exists and will be overwritten.");
		if (ImGui::Button("Save", ImVec2(120, 0)) || enter) {
			bool saved = do_save(ed, ed->pathInput);
			ImGui::CloseCurrentPopup();
			if (saved && ed->afterSaveTest)
				launch_game(ed);
			ed->afterSaveTest = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			ed->afterSaveTest = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Unsaved changes", nullptr, modal)) {
		ImGui::Text("%s has unsaved changes.", doc_name(&ed->doc));
		bool go = false;
		if (ImGui::Button("Save", ImVec2(110, 0))) {
			if (ed->doc.path[0]) {
				go = do_save(ed, ed->doc.path);
			} else {
				ed->pending = PENDING_NONE;
				save_or_ask(ed);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard", ImVec2(110, 0))) {
			ed->doc.dirty = false;
			go = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			ed->pending = PENDING_NONE;
			ImGui::CloseCurrentPopup();
		}
		if (go) {
			Pending p = ed->pending;
			ed->pending = PENDING_NONE;
			request(ed, p);
		}
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Resize map", nullptr, modal)) {
		ImGui::InputInt("Width", &ed->resizeWidth);
		ImGui::InputInt("Height", &ed->resizeHeight);
		ed->resizeWidth = std::max((int)DOC_MIN_SIZE, std::min((int)DOC_MAX_SIZE, ed->resizeWidth));
		ed->resizeHeight = std::max((int)DOC_MIN_SIZE, std::min((int)DOC_MAX_SIZE, ed->resizeHeight));
		ImGui::TextUnformatted("Keep the map at:");
		static const char *ARROWS[9] = { "NW", "N", "NE", "W", "C", "E", "SW", "S", "SE" };
		for (int i = 0; i < 9; i++) {
			if (i % 3)
				ImGui::SameLine();
			ImGui::PushID(i);
			if (ImGui::Selectable(ARROWS[i], ed->resizeAnchor == i, 0, ImVec2(32, 24)))
				ed->resizeAnchor = i;
			ImGui::PopID();
		}
		ImGui::TextDisabled("New cells are floor; use Wall in border afterwards.");
		if (ImGui::Button("Resize", ImVec2(120, 0))) {
			int ax = ed->resizeAnchor % 3, ay = ed->resizeAnchor / 3;
			int dx = ed->resizeWidth - ed->doc.width, dy = ed->resizeHeight - ed->doc.height;
			int ox = ax == 0 ? 0 : ax == 1 ? dx / 2 : dx;
			int oy = ay == 0 ? 0 : ay == 1 ? dy / 2 : dy;
			doc_begin_edit(&ed->doc);
			doc_resize(&ed->doc, ed->resizeWidth, ed->resizeHeight, ox, oy);
			doc_end_edit(&ed->doc);
			ed->fitRequested = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Message", nullptr, modal)) {
		ImGui::TextUnformatted(ed->message);
		if (ImGui::Button("OK", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)
			|| ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

/* Default layout: palette left, map in the middle, properties and problems right. */
static void build_layout(ImGuiID dock, ImVec2 size)
{
	ImGui::DockBuilderRemoveNode(dock);
	ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dock, size);
	ImGuiID left, rest, right, center, rightTop, rightBottom;
	ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.22f, &left, &rest);
	ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.26f, &right, &center);
	ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.6f, &rightTop, &rightBottom);
	ImGui::DockBuilderDockWindow("Palette", left);
	ImGui::DockBuilderDockWindow("Map", center);
	ImGui::DockBuilderDockWindow("Properties", rightTop);
	ImGui::DockBuilderDockWindow("###Problems", rightBottom);
	ImGui::DockBuilderFinish(dock);
}

/* --------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------ */

static void glfw_error(int code, const char *text)
{
	fprintf(stderr, "GLFW error %d: %s\n", code, text);
}

int main(int argc, char **argv)
{
	if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
		fprintf(stderr, "usage: %s [map.txt]\n", argv[0]);
		return 1;
	}

	glfwSetErrorCallback(glfw_error);
	if (!glfwInit())
		return 1;
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
	GLFWwindow *window = glfwCreateWindow(1600, 900, "Raycaster Map Editor", nullptr, nullptr);
	if (!window) {
		glfwTerminate();
		return 1;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Could not load OpenGL functions\n");
		return 1;
	}
	glGetError();	/* GLEW can leave GL_INVALID_ENUM behind on core profiles */

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;	/* no keyboard nav: Space pans the map */
	io.IniFilename = INI_FILE;
	ImGui::StyleColorsDark();
	float xscale = 1.0f, yscale = 1.0f;
	glfwGetWindowContentScale(window, &xscale, &yscale);
	if (xscale > 1.0f) {
		ImGui::GetStyle().ScaleAllSizes(xscale);
		ImGui::GetStyle().FontScaleDpi = xscale;
	}
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330 core");

	static Editor ed;
	ed.tool = TOOL_RECT;
	ed.brush = 0;
	ed.zoom = 16.0f;
	ed.showGrid = true;
	ed.showTextures = true;
	ed.showReach = false;
	ed.focusX = ed.focusY = -1;
	ed.newWidth = 32;
	ed.newHeight = 32;
	ed.testX = ed.testY = -1;
	ed.validatedRevision = ~0u;
	find_game(&ed);
	snprintf(ed.status, sizeof(ed.status), "Ready");

	if (!art_load(&ed.art)) {
		fprintf(stderr, "Could not load textures from assets/: run the editor from the repo root\n");
		return 1;
	}

	config_defaults(&ed.config);
	config_load(&ed.config, CONFIG_FILE);	/* missing is fine: defaults */
	if (!audio_init(&ed.audio, ed.config.sfxVolume))
		fprintf(stderr, "Audio disabled: music preview will not work\n");

	doc_new(&ed.doc, 32, 32);
	if (argc == 2)
		do_open(&ed, argv[1]);
	ed.fitRequested = true;
	bool layoutChecked = false;

	while (!ed.quit) {
		glfwPollEvents();
		if (glfwWindowShouldClose(window)) {
			glfwSetWindowShouldClose(window, GLFW_FALSE);
			request(&ed, PENDING_QUIT);
		}
		reap_game(&ed);
		audio_update(&ed.audio);

		/* Undo, redo or opening another map can change the song under a running preview. */
		static char previewed[DOC_MUSIC_MAX];
		if (ed.previewing && strcmp(previewed, ed.doc.music) != 0)
			start_preview(&ed);
		memcpy(previewed, ed.doc.music, sizeof(previewed));

		if (ed.validatedRevision != ed.doc.revision) {
			doc_validate(&ed.doc, &ed.problems, &ed.stats, &ed.reach);
			ed.validatedRevision = ed.doc.revision;
		}

		char title[600];
		snprintf(title, sizeof(title), "%s%s - Raycaster Map Editor", doc_name(&ed.doc), ed.doc.dirty ? " *" : "");
		glfwSetWindowTitle(window, title);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		draw_menu(&ed);
		ImGuiID dock = ImHashStr("EditorDockSpace");
		if (!layoutChecked || ed.resetLayout) {
			/* First run (no map_editor.ini yet) or View > Reset: build the default layout. */
			ImGuiDockNode *node = ImGui::DockBuilderGetNode(dock);
			if (ed.resetLayout || !node || node->IsLeafNode())
				build_layout(dock, ImGui::GetMainViewport()->WorkSize);
			layoutChecked = true;
			ed.resetLayout = false;
		}
		ImGui::DockSpaceOverViewport(dock, ImGui::GetMainViewport());

		handle_shortcuts(&ed);
		draw_palette_window(&ed);
		draw_properties_window(&ed);
		draw_problems_window(&ed);
		draw_map_window(&ed);
		draw_dialogs(&ed);

		ImGui::Render();
		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
		glViewport(0, 0, w, h);
		glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);
	}

	audio_shutdown(&ed.audio);
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
