#include "Sprites.h"

#include "AsmRoutines.h"
#include "Game.h"
#include "Palette.h"
#include "Raycaster.h"
#include "Textures.h"
#include "Video.h"

#include <math.h>

static const float NEAR_CLIP = 0.1f;	/* closer than this the sprite would blow up */

/* A sprite that survived culling, in camera space. */
struct VisSprite {
	float depth;		/* distance along the view direction */
	float screenX;		/* column of the sprite centre */
	const Texture *tex;
};

static int clamp_int(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t to_fixed(double v)
{
	return (uint32_t)(int64_t)floor(v * 65536.0);
}

static void draw_sprite(const Raycaster *rc, Video *video, const RenderAssets *assets, const VisSprite *s)
{
	const int w = rc->width;
	const int h = rc->height;
	const float horizon = (float)h * 0.5f;
	const Texture *tex = s->tex;

	/* 1 world unit wide and tall; pixel aspect is already in focalX vs focalY. */
	float spriteW = rc->focalX / s->depth;
	float spriteH = rc->focalY / s->depth;
	float left = s->screenX - spriteW * 0.5f;
	float top = horizon - spriteH * 0.5f;

	/* Columns and rows whose pixel centres fall inside the sprite rectangle. */
	int x0 = clamp_int((int)ceilf(left - 0.5f), 0, w);
	int x1 = clamp_int((int)ceilf(left + spriteW - 0.5f), 0, w);
	int y0 = clamp_int((int)ceilf(top - 0.5f), 0, h);
	int y1 = clamp_int((int)ceilf(top + spriteH - 0.5f), 0, h);
	if (x1 <= x0 || y1 <= y0)
		return;

	double texelsPerRow = (double)tex->height / (double)spriteH;
	double texelsPerCol = (double)tex->width / (double)spriteW;

	ColumnArgs args;
	args.colormap = assets->colormaps->level[raycaster_light_level(s->depth)];
	args.pitch = video->pitch;
	args.count = y1 - y0;
	args.frac = to_fixed(((double)y0 + 0.5 - (double)top) * texelsPerRow);
	args.step = to_fixed(texelsPerRow);
	args.mask = (uint32_t)(tex->height - 1);
	args.pad = 0;

	for (int x = x0; x < x1; x++) {
		/* Hidden by a wall in this column? Walls are full height, so it is all or nothing. */
		if (s->depth >= rc->zbuffer[x])
			continue;
		int u = (int)(((double)x + 0.5 - (double)left) * texelsPerCol);
		u = clamp_int(u, 0, tex->width - 1);
		args.dest = video->fb + y0 * video->pitch + x;
		args.source = texture_column(tex, u);
		RC_draw_sprite_col(&args);
	}
}

void sprites_render(const Raycaster *rc, Video *video, const RenderAssets *assets,
	const Entity *entities, int count, glm::vec2 pos, float angle)
{
	static VisSprite vis[MAX_ENTITIES];
	int visCount = 0;

	const glm::vec2 dir(cosf(angle), sinf(angle));
	const glm::vec2 right(-dir.y, dir.x);
	const float halfW = (float)rc->width * 0.5f;

	for (int i = 0; i < count && visCount < MAX_ENTITIES; i++) {
		if (!entities[i].active)
			continue;
		glm::vec2 rel = entities[i].pos - pos;
		float depth = rel.x * dir.x + rel.y * dir.y;
		if (depth < NEAR_CLIP)
			continue;
		float lateral = rel.x * right.x + rel.y * right.y;
		float screenX = halfW + lateral * rc->focalX / depth;

		/* Off either side of the screen, half a sprite width of slack. */
		float halfSprite = rc->focalX * 0.5f / depth;
		if (screenX + halfSprite < 0.0f || screenX - halfSprite > (float)rc->width)
			continue;

		VisSprite *v = &vis[visCount++];
		v->depth = depth;
		v->screenX = screenX;
		v->tex = &assets->sprites->sprite[entities[i].sprite];
	}

	/* Painter's order, far to near. Insertion sort: few sprites, nearly sorted frame to frame. */
	for (int i = 1; i < visCount; i++) {
		VisSprite key = vis[i];
		int j = i - 1;
		while (j >= 0 && vis[j].depth < key.depth) {
			vis[j + 1] = vis[j];
			j--;
		}
		vis[j + 1] = key;
	}

	for (int i = 0; i < visCount; i++)
		draw_sprite(rc, video, assets, &vis[i]);
}
