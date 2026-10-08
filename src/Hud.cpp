#include "Hud.h"

#include "AsmRoutines.h"
#include "Game.h"
#include "Palette.h"
#include "Raycaster.h"
#include "Textures.h"
#include "Video.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Default palette indices (ramp * 16 + shade). */
static const uint8_t BLACK = 0;
static const uint8_t WHITE = 15;
static const uint8_t LABEL = 10;
static const uint8_t RED = 1 * 16 + 14;
static const uint8_t YELLOW = 3 * 16 + 14;
static const uint8_t GREEN = 5 * 16 + 14;
static const int BAR_RAMP = 12;			/* blue steel, like Wolf3D's status bar */
static const int DEAD_DARKEN = 18;		/* colormap level for the view when dead */
static const float WEAPON_WIDTH = 128.0f;	/* columns at 320x200 */

/* ------------------------------------------------------------------------- */
/* 5x7 bitmap font: 7 rows per glyph, bit 4 is the leftmost pixel            */
/* ------------------------------------------------------------------------- */

struct Glyph {
	char c;
	uint8_t rows[7];
};

static const Glyph FONT[] = {
	{ '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
	{ '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
	{ '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
	{ '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
	{ '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
	{ '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
	{ '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
	{ '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
	{ '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
	{ '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
	{ 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
	{ 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
	{ 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
	{ 'D', { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
	{ 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
	{ 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
	{ 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
	{ 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
	{ 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
	{ 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
	{ 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
	{ 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
	{ 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
	{ 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
	{ 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	{ 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
	{ 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
	{ 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
	{ 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
	{ 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
	{ 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	{ 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
	{ 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
	{ 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
	{ 'Y', { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 } },
	{ 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
	{ '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
	{ '/', { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 } },
	{ '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
	{ '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
	{ '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
	{ ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
	{ '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
};

static const uint8_t *glyph_rows(char c)
{
	for (size_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); i++) {
		if (FONT[i].c == c)
			return FONT[i].rows;
	}
	return nullptr;	/* space and anything unknown draw nothing */
}

/* ------------------------------------------------------------------------- */
/* Primitives                                                                */
/* ------------------------------------------------------------------------- */

static void fill_rect(Video *video, int x0, int y0, int x1, int y1, uint8_t color)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > video->width) x1 = video->width;
	if (y1 > video->height) y1 = video->height;
	for (int y = y0; y < y1; y++)
		memset(video->fb + y * video->pitch + x0, color, (size_t)(x1 > x0 ? x1 - x0 : 0));
}

/* Each font pixel becomes an sx by sy block. */
static void draw_text(Video *video, int x, int y, const char *text, int sx, int sy, uint8_t color)
{
	for (const char *c = text; *c; c++, x += 6 * sx) {
		const uint8_t *rows = glyph_rows(*c);
		if (!rows)
			continue;
		for (int r = 0; r < 7; r++) {
			for (int b = 0; b < 5; b++) {
				if (rows[r] & (0x10 >> b))
					fill_rect(video, x + b * sx, y + r * sy, x + (b + 1) * sx, y + (r + 1) * sy, color);
			}
		}
	}
}

static int text_width(const char *text, int sx)
{
	int n = (int)strlen(text);
	return n > 0 ? n * 6 * sx - sx : 0;
}

/* Text with a one-block drop shadow so it reads over any background. */
static void draw_text_shadow(Video *video, int x, int y, const char *text, int sx, int sy, uint8_t color)
{
	draw_text(video, x + sx, y + sy, text, sx, sy, BLACK);
	draw_text(video, x, y, text, sx, sy, color);
}

/* Sunken panel: dark inside, shadow top-left, highlight bottom-right. */
static void draw_panel(Video *video, int x0, int y0, int x1, int y1, int s)
{
	fill_rect(video, x0, y0, x1, y1, (uint8_t)(BAR_RAMP * 16 + 9));
	fill_rect(video, x0, y0, x1 - s, y1 - s, (uint8_t)(BAR_RAMP * 16 + 1));
	fill_rect(video, x0 + s, y0 + s, x1 - s, y1 - s, (uint8_t)(BAR_RAMP * 16 + 2));
}

/* ------------------------------------------------------------------------- */
/* Pieces                                                                    */
/* ------------------------------------------------------------------------- */

static void draw_weapon(Video *video, const Raycaster *rc, const Game *game, const RenderAssets *assets)
{
	const Player *p = &game->player;
	static const uint8_t FRAMES[WEAPON_COUNT][2] = {	/* idle, attacking */
		{ SPR_FISTS_IDLE, SPR_FISTS_PUNCH },
		{ SPR_WEAPON_IDLE, SPR_WEAPON_FIRE },
		{ SPR_SMG_IDLE, SPR_SMG_FIRE },
	};
	const Texture *tex = &assets->sprites->sprite[FRAMES[p->weapon][p->flashTime > 0.0f ? 1 : 0]];
	const float s = (float)video->width / (float)VIDEO_BASE_WIDTH;

	/* Same pixel-aspect correction as the world: rows are 1.2x taller than columns are wide. */
	float pixelAspect = ((float)video->width / (float)video->height) / VIDEO_DISPLAY_ASPECT;
	float drawW = WEAPON_WIDTH * s;
	float drawH = drawW / pixelAspect;

	/* Walk bob: sway sideways, dip twice per sway. */
	float bobX = sinf(p->bobPhase) * 6.0f * s;
	float bobY = fabsf(cosf(p->bobPhase)) * 5.0f * s;
	float left = (float)rc->width * 0.5f - drawW * 0.5f + bobX;
	float top = (float)rc->height - drawH + bobY + game_weapon_lower(p) * drawH;

	int x0 = (int)ceilf(left - 0.5f), x1 = (int)ceilf(left + drawW - 0.5f);
	int y0 = (int)ceilf(top - 0.5f);
	if (x0 < 0) x0 = 0;
	if (x1 > rc->width) x1 = rc->width;
	if (y0 < 0) y0 = 0;
	int y1 = rc->height;
	if (y1 <= y0)
		return;

	double texelsPerRow = (double)tex->height / (double)drawH;
	ColumnArgs args;
	args.colormap = assets->colormaps->level[0];	/* full bright: it is right in front of you */
	args.pitch = video->pitch;
	args.count = y1 - y0;
	args.frac = (uint32_t)(int64_t)floor(((double)y0 + 0.5 - (double)top) * texelsPerRow * 65536.0);
	args.step = (uint32_t)(int64_t)floor(texelsPerRow * 65536.0);
	args.mask = (uint32_t)(tex->height - 1);
	args.pad = 0;
	for (int x = x0; x < x1; x++) {
		int u = (int)(((float)x + 0.5f - left) * (float)tex->width / drawW);
		if (u < 0) u = 0;
		if (u >= tex->width) u = tex->width - 1;
		args.dest = video->fb + y0 * video->pitch + x;
		args.source = texture_column(tex, u);
		RC_draw_sprite_col(&args);
	}
}

static void draw_status_bar(Video *video, const Raycaster *rc, const Game *game)
{
	const int s = video->width / VIDEO_BASE_WIDTH;
	const int top = rc->height;
	const Player *p = &game->player;
	char text[32];

	/* Steel background with a lit top edge. */
	fill_rect(video, 0, top, video->width, video->height, (uint8_t)(BAR_RAMP * 16 + 5));
	fill_rect(video, 0, top, video->width, top + s, (uint8_t)(BAR_RAMP * 16 + 11));
	fill_rect(video, 0, top + s, video->width, top + 2 * s, (uint8_t)(BAR_RAMP * 16 + 3));

	struct Box { int x0, x1; const char *label; };
	const Box boxes[4] = {
		{ 6, 80, "HEALTH" }, { 86, 160, "KILLS" }, { 166, 236, "ARMS" }, { 242, 314, "AMMO" }
	};
	for (const Box &b : boxes) {
		draw_panel(video, b.x0 * s, top + 4 * s, b.x1 * s, top + 29 * s, s);
		int cx = (b.x0 + b.x1) / 2 * s;
		draw_text(video, cx - text_width(b.label, s) / 2, top + 6 * s, b.label, s, s, LABEL);
	}

	uint8_t healthColor = p->health > 50 ? GREEN : (p->health > 25 ? YELLOW : RED);
	snprintf(text, sizeof(text), "%d%%", p->health);
	draw_text_shadow(video, 43 * s - text_width(text, 2 * s) / 2, top + 14 * s, text, 2 * s, 2 * s, healthColor);

	snprintf(text, sizeof(text), "%d/%d", game->enemiesKilled, game->enemiesTotal);
	draw_text_shadow(video, 123 * s - text_width(text, 2 * s) / 2, top + 14 * s, text, 2 * s, 2 * s, WHITE);

	/* Doom's ARMS: slot numbers, the one in hand bright, owned ones dim, the rest hidden. */
	int wanted = p->pendingWeapon != WEAPON_NONE ? p->pendingWeapon : p->weapon;
	for (int w = 0; w < WEAPON_COUNT; w++) {
		if (!((p->weaponsOwned >> w) & 1u))
			continue;
		char digit[2] = { (char)('1' + w), '\0' };
		int x = (201 + (w - 1) * 20) * s - text_width(digit, 2 * s) / 2;
		draw_text_shadow(video, x, top + 14 * s, digit, 2 * s, 2 * s, w == wanted ? YELLOW : LABEL);
	}

	snprintf(text, sizeof(text), "%d", p->ammo);
	draw_text_shadow(video, 278 * s - text_width(text, 2 * s) / 2, top + 14 * s, text, 2 * s, 2 * s,
		p->ammo > 0 ? YELLOW : RED);
}

/* Darken the 3D view through a light table, for the screens drawn over it. */
static void dim_view(Video *video, const Raycaster *rc, const RenderAssets *assets)
{
	const uint8_t *dim = assets->colormaps->level[DEAD_DARKEN];
	for (int y = 0; y < rc->height; y++) {
		uint8_t *row = video->fb + y * video->pitch;
		for (int x = 0; x < rc->width; x++)
			row[x] = dim[row[x]];
	}
}

/* Dim the 3D view and put the restart prompt on it. */
static void draw_death_screen(Video *video, const Raycaster *rc, const RenderAssets *assets)
{
	const int s = video->width / VIDEO_BASE_WIDTH;
	dim_view(video, rc, assets);
	const char *title = "YOU DIED";
	const char *hint = "PRESS SPACE TO RESTART";
	int cy = rc->height / 2;
	draw_text_shadow(video, (rc->width - text_width(title, 3 * s)) / 2, cy - 24 * s, title, 3 * s, 3 * s, RED);
	draw_text_shadow(video, (rc->width - text_width(hint, s)) / 2, cy + 6 * s, hint, s, s, WHITE);
}

static int percent(int part, int total)
{
	return total > 0 ? part * 100 / total : 100;
}

/*
 * Wolf3D's "floor complete" tally on the dimmed view: kill, secret and item
 * ratios and the time taken. After the last map it says so and offers to
 * play the episode again.
 */
static void draw_level_end(Video *video, const Raycaster *rc, const Game *game, const RenderAssets *assets)
{
	const int s = video->width / VIDEO_BASE_WIDTH;
	const bool last = game->phase == PHASE_FINISHED;
	dim_view(video, rc, assets);

	const char *title = last ? "EPISODE COMPLETE!" : "FLOOR COMPLETE";
	int y = 14 * s;
	int titleScale = last ? 2 : 3;
	draw_text_shadow(video, (rc->width - text_width(title, titleScale * s)) / 2, y, title,
		titleScale * s, titleScale * s, last ? YELLOW : GREEN);
	y += (titleScale * 7 + 14) * s;

	char value[32];
	unsigned seconds = game->levelTicks / GAME_TICK_RATE;
	struct Row { const char *label; int pct; };
	const Row rows[] = {
		{ "KILLS", percent(game->enemiesKilled, game->enemiesTotal) },
		{ "SECRETS", percent(game->secretsFound, game->secretsTotal) },
		{ "ITEMS", percent(game->itemsTaken, game->itemsTotal) },
		{ "TIME", -1 },
	};
	const int left = rc->width / 2 - 80 * s, right = rc->width / 2 + 80 * s;
	for (const Row &r : rows) {
		if (r.pct >= 0)
			snprintf(value, sizeof(value), "%d%%", r.pct);
		else
			snprintf(value, sizeof(value), "%u:%02u", seconds / 60, seconds % 60);
		uint8_t color = r.pct == 100 ? GREEN : WHITE;
		draw_text_shadow(video, left, y, r.label, 2 * s, 2 * s, LABEL);
		draw_text_shadow(video, right - text_width(value, 2 * s), y, value, 2 * s, 2 * s, color);
		y += 20 * s;
	}

	/* The prompt appears once Use is accepted, so a held key does not skip the tally. */
	if (game->phaseTime >= GAME_INTERMISSION_DELAY) {
		const char *hint = last ? "PRESS SPACE TO PLAY AGAIN" : "PRESS SPACE TO CONTINUE";
		draw_text_shadow(video, (rc->width - text_width(hint, s)) / 2, y + 4 * s, hint, s, s, YELLOW);
	}
}

void hud_draw(Video *video, const Raycaster *rc, const Game *game, const RenderAssets *assets,
	const char *stats)
{
	const int s = video->width / VIDEO_BASE_WIDTH;
	if (game->phase != PHASE_PLAYING)
		draw_level_end(video, rc, game, assets);
	else if (game->player.dead)
		draw_death_screen(video, rc, assets);
	else
		draw_weapon(video, rc, game, assets);
	if (game->message)
		draw_text_shadow(video, 4 * s, 4 * s, game->message, s, s, WHITE);
	if (stats)
		draw_text_shadow(video, rc->width - text_width(stats, s) - 5 * s, 4 * s, stats, s, s, YELLOW);
	draw_status_bar(video, rc, game);
}
