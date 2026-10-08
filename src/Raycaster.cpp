#include "Raycaster.h"

#include "AsmRoutines.h"
#include "Map.h"
#include "Palette.h"
#include "Textures.h"
#include "Video.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const float EYE_HEIGHT = 0.5f;		/* walls are 1 unit tall, eye at mid-height */
static const float LIGHT_PER_UNIT = 1.6f;	/* colormap levels added per unit of distance */
static const int LIGHT_MAX = 27;		/* darkest level distance shading reaches */
static const int LIGHT_SIDE_Y = 4;		/* extra darkness for north/south faces (Wolf3D side shading) */
static const int MAX_DDA_STEPS = 1024;		/* safety net; the solid border stops rays first */

bool raycaster_init(Raycaster *rc, const Video *video, float hfovDegrees, int viewHeight)
{
	memset(rc, 0, sizeof(*rc));
	rc->width = video->width;
	rc->height = viewHeight;
	rc->planeLen = tanf(hfovDegrees * 0.5f * 3.14159265f / 180.0f);
	rc->focalX = (float)rc->width * 0.5f / rc->planeLen;

	/* Displayed height / width of one framebuffer pixel: 1.2 for 320x200 at 4:3. */
	float pixelAspect = ((float)video->width / (float)video->height) / VIDEO_DISPLAY_ASPECT;
	rc->focalY = rc->focalX / pixelAspect;

	rc->zbuffer = (float *)calloc((size_t)rc->width, sizeof(float));
	rc->wallTop = (int *)calloc((size_t)rc->width, sizeof(int));
	rc->wallBottom = (int *)calloc((size_t)rc->width, sizeof(int));
	if (!rc->zbuffer || !rc->wallTop || !rc->wallBottom) {
		raycaster_shutdown(rc);
		return false;
	}
	return true;
}

void raycaster_shutdown(Raycaster *rc)
{
	free(rc->zbuffer);
	free(rc->wallTop);
	free(rc->wallBottom);
	rc->zbuffer = nullptr;
	rc->wallTop = nullptr;
	rc->wallBottom = nullptr;
}

static int clamp_int(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static int light_level(float distance, int extra)
{
	int level = (int)(distance * LIGHT_PER_UNIT) + extra;
	return level > LIGHT_MAX ? LIGHT_MAX : level;
}

int raycaster_light_level(float distance)
{
	return light_level(distance, 0);
}

static int log2_pow2(int v)
{
	int n = 0;
	while ((1 << n) < v)
		n++;
	return n;
}

/* 16.16 fixed point from a real value; wraps mod 2^32 like the inner loops. */
static uint32_t to_fixed(double v)
{
	return (uint32_t)(int64_t)floor(v * 65536.0);
}

/* ------------------------------------------------------------------------- */
/* Pass 1: walls                                                             */
/* ------------------------------------------------------------------------- */

/*
 * Ray against a door panel on its cell's mid-plane. tEnter/tExit bound the
 * part of the ray inside the door cell. The open part of the door (the first
 * `open` of the cell along the panel) lets the ray through; the texture
 * slides with the panel.
 */
static bool ray_hits_door(const Door *d, glm::vec2 pos, glm::vec2 ray, float tEnter, float tExit,
	float *t, float *u, int *side)
{
	float tPlane, along;
	if (d->vertical) {
		if (ray.x == 0.0f)
			return false;
		tPlane = ((float)d->x + 0.5f - pos.x) / ray.x;
		along = pos.y + tPlane * ray.y - (float)d->y;
	} else {
		if (ray.y == 0.0f)
			return false;
		tPlane = ((float)d->y + 0.5f - pos.y) / ray.y;
		along = pos.x + tPlane * ray.x - (float)d->x;
	}
	if (tPlane < tEnter || tPlane > tExit)
		return false;
	if (along < d->open || along >= 1.0f)
		return false;
	*t = tPlane;
	*u = along - d->open;
	*side = d->vertical ? 0 : 1;
	return true;
}

/*
 * Ray against the moving push wall, a 1x1 box between two cells (slab test).
 * Only counts if the ray enters the box inside the current cell; the other
 * covered cell gets its own test.
 */
static bool ray_hits_pushwall(const Pushwall *pw, glm::vec2 pos, glm::vec2 ray, float tEnter, float tExit,
	float *t, float *u, int *side)
{
	const float EPS = 1e-4f;
	float bx = (float)pw->x + (float)pw->dx * pw->offset;
	float by = (float)pw->y + (float)pw->dy * pw->offset;

	float tx0 = -1e30f, tx1 = 1e30f, ty0 = -1e30f, ty1 = 1e30f;
	if (ray.x != 0.0f) {
		tx0 = (bx - pos.x) / ray.x;
		tx1 = (bx + 1.0f - pos.x) / ray.x;
		if (tx0 > tx1) { float tmp = tx0; tx0 = tx1; tx1 = tmp; }
	} else if (pos.x < bx || pos.x > bx + 1.0f) {
		return false;
	}
	if (ray.y != 0.0f) {
		ty0 = (by - pos.y) / ray.y;
		ty1 = (by + 1.0f - pos.y) / ray.y;
		if (ty0 > ty1) { float tmp = ty0; ty0 = ty1; ty1 = tmp; }
	} else if (pos.y < by || pos.y > by + 1.0f) {
		return false;
	}

	float tNear = tx0 > ty0 ? tx0 : ty0;
	float tFar = tx1 < ty1 ? tx1 : ty1;
	if (tNear > tFar || tNear < tEnter - EPS || tNear > tExit + EPS)
		return false;

	*side = tx0 > ty0 ? 0 : 1;
	float along = *side == 0 ? pos.y + tNear * ray.y - by : pos.x + tNear * ray.x - bx;
	*u = along < 0.0f ? 0.0f : (along > 0.9999f ? 0.9999f : along);
	*t = tNear;
	return true;
}

static void draw_walls(Raycaster *rc, Video *video, const Map *map, const RenderAssets *assets,
	glm::vec2 pos, glm::vec2 dir, glm::vec2 plane)
{
	const int w = rc->width;
	const int h = rc->height;
	const float horizon = (float)h * 0.5f;

	for (int x = 0; x < w; x++) {
		/* -1 at the left edge of the screen, +1 at the right, sampled at pixel centres. */
		float cameraX = 2.0f * ((float)x + 0.5f) / (float)w - 1.0f;
		glm::vec2 ray = dir + plane * cameraX;

		int mapX = (int)floorf(pos.x);
		int mapY = (int)floorf(pos.y);
		float deltaX = ray.x != 0.0f ? fabsf(1.0f / ray.x) : 1e30f;
		float deltaY = ray.y != 0.0f ? fabsf(1.0f / ray.y) : 1e30f;

		int stepX, stepY;
		float sideX, sideY;
		if (ray.x < 0.0f) {
			stepX = -1;
			sideX = (pos.x - (float)mapX) * deltaX;
		} else {
			stepX = 1;
			sideX = ((float)mapX + 1.0f - pos.x) * deltaX;
		}
		if (ray.y < 0.0f) {
			stepY = -1;
			sideY = (pos.y - (float)mapY) * deltaY;
		} else {
			stepY = 1;
			sideY = ((float)mapY + 1.0f - pos.y) * deltaY;
		}

		int side = 0;
		float perp = 0.0f;
		float hitU = 0.0f;	/* 0..1 across the face that was hit, before flipping */
		const Texture *tex = nullptr;
		bool inDoorCell = map_tile(map, mapX, mapY) == TILE_DOOR;
		for (int i = 0; i < MAX_DDA_STEPS; i++) {
			if (sideX < sideY) {
				sideX += deltaX;
				mapX += stepX;
				side = 0;
			} else {
				sideY += deltaY;
				mapY += stepY;
				side = 1;
			}
			uint8_t tile = map_tile(map, mapX, mapY);
			if (tile == TILE_EMPTY) {
				inDoorCell = false;
				continue;
			}

			/* Ray parameter where it entered and will leave this cell; it equals perpendicular distance. */
			float tEnter = side == 0 ? sideX - deltaX : sideY - deltaY;
			float tExit = sideX < sideY ? sideX : sideY;

			if (tile == TILE_DOOR) {
				if (ray_hits_door(map_door(map, mapX, mapY), pos, ray, tEnter, tExit, &perp, &hitU, &side)) {
					tex = &assets->walls->tile[TILE_DOOR];
					break;
				}
				inDoorCell = true;	/* through the gap: walls seen from here are door frames */
				continue;
			}
			if (tile == TILE_PUSHWALL) {
				if (ray_hits_pushwall(&map->pushwall, pos, ray, tEnter, tExit, &perp, &hitU, &side)) {
					tex = wall_texture(assets->walls, TILE_SECRET);
					break;
				}
				inDoorCell = false;
				continue;
			}

			/* Solid wall: hit on the cell boundary. */
			perp = tEnter;
			float wallX = side == 0 ? pos.y + perp * ray.y : pos.x + perp * ray.x;
			hitU = wallX - floorf(wallX);
			tex = inDoorCell ? &assets->walls->doorJamb : wall_texture(assets->walls, tile);
			break;
		}
		if (!tex) {
			/* Ray escaped (cannot happen with a solid border): treat as infinitely far. */
			rc->zbuffer[x] = 1e30f;
			rc->wallTop[x] = rc->wallBottom[x] = h / 2;
			continue;
		}

		/* Distance along the view direction, not the ray: no fisheye. */
		if (perp < 1e-4f)
			perp = 1e-4f;
		rc->zbuffer[x] = perp;

		/* Pixel y is drawn if its centre is inside the wall. */
		float lineH = rc->focalY / perp;
		float top = horizon - lineH * 0.5f;
		int yStart = clamp_int((int)ceilf(top - 0.5f), 0, h);
		int yEnd = clamp_int((int)ceilf(horizon + lineH * 0.5f - 0.5f), 0, h);
		rc->wallTop[x] = yStart;
		rc->wallBottom[x] = yEnd;
		if (yEnd <= yStart)
			continue;

		/* Texture u, flipped so the texture reads left to right from either side. */
		int u = (int)(hitU * (float)tex->width);
		u = clamp_int(u, 0, tex->width - 1);
		if ((side == 0 && ray.x < 0.0f) || (side == 1 && ray.y > 0.0f))
			u = tex->width - 1 - u;

		/* Texture v at the centre of the first drawn pixel. */
		double texelsPerRow = (double)tex->height / (double)lineH;
		int light = light_level(perp, side == 0 ? 0 : LIGHT_SIDE_Y);

		ColumnArgs args;
		args.dest = video->fb + yStart * video->pitch + x;
		args.source = texture_column(tex, u);
		args.colormap = assets->colormaps->level[light];
		args.pitch = video->pitch;
		args.count = yEnd - yStart;
		args.frac = to_fixed(((double)yStart + 0.5 - (double)top) * texelsPerRow);
		args.step = to_fixed(texelsPerRow);
		args.mask = (uint32_t)(tex->height - 1);
		args.pad = 0;
		RC_draw_column(&args);
	}
}

/* ------------------------------------------------------------------------- */
/* Pass 2: floor and ceiling spans                                           */
/* ------------------------------------------------------------------------- */

/*
 * Every pixel in one screen row of the floor (or ceiling) is the same
 * distance from the camera plane, so the row is a straight line across the
 * world: a start point plus a constant step per pixel, and one light level.
 * The wall pass left wallTop/wallBottom per column; each row is drawn as
 * runs of the columns where no wall covers it.
 */
static void draw_flats(Raycaster *rc, Video *video, const RenderAssets *assets,
	glm::vec2 pos, glm::vec2 dir, glm::vec2 plane)
{
	const int w = rc->width;
	const int h = rc->height;
	const double horizon = (double)h * 0.5;

	for (int y = 0; y < h; y++) {
		double centre = (double)y + 0.5;
		bool isFloor = centre > horizon;
		double rowsFromHorizon = isFloor ? centre - horizon : horizon - centre;
		if (rowsFromHorizon <= 0.0)
			continue;

		/* A point EYE_HEIGHT below (or above) the eye shows up rowsFromHorizon rows away. */
		double rowDist = (double)EYE_HEIGHT * (double)rc->focalY / rowsFromHorizon;
		const Texture *tex = isFloor ? &assets->flats->floor : &assets->flats->ceiling;
		const uint8_t *colormap = assets->colormaps->level[light_level((float)rowDist, 0)];

		/* World position per screen pixel along this row. */
		double stepX = rowDist * (double)plane.x * 2.0 / (double)w;
		double stepY = rowDist * (double)plane.y * 2.0 / (double)w;

		SpanArgs args;
		args.source = tex->columns;
		args.colormap = colormap;
		args.ustep = to_fixed(stepX * (double)tex->width);
		args.vstep = to_fixed(stepY * (double)tex->height);
		args.umask = (uint32_t)(tex->width - 1);
		args.vmask = (uint32_t)(tex->height - 1);
		args.vshift = (uint32_t)log2_pow2(tex->height);

		uint8_t *row = video->fb + y * video->pitch;
		int x = 0;
		while (x < w) {
			/* Skip columns where a wall covers this row. */
			while (x < w && (isFloor ? rc->wallBottom[x] > y : rc->wallTop[x] <= y))
				x++;
			int start = x;
			while (x < w && (isFloor ? rc->wallBottom[x] <= y : rc->wallTop[x] > y))
				x++;
			if (x == start)
				continue;

			double cameraX = 2.0 * ((double)start + 0.5) / (double)w - 1.0;
			double worldX = (double)pos.x + rowDist * ((double)dir.x + (double)plane.x * cameraX);
			double worldY = (double)pos.y + rowDist * ((double)dir.y + (double)plane.y * cameraX);

			args.dest = row + start;
			args.count = x - start;
			args.ufrac = to_fixed(worldX * (double)tex->width);
			args.vfrac = to_fixed(worldY * (double)tex->height);
			RC_draw_span(&args);
		}
	}
}

void raycaster_render(Raycaster *rc, Video *video, const Map *map,
	const RenderAssets *assets, glm::vec2 pos, float angle)
{
	const glm::vec2 dir(cosf(angle), sinf(angle));
	const glm::vec2 plane = glm::vec2(-dir.y, dir.x) * rc->planeLen;	/* camera right */

	draw_walls(rc, video, map, assets, pos, dir, plane);
	draw_flats(rc, video, assets, pos, dir, plane);
}
