#ifndef RAYCASTER_H
#define RAYCASTER_H

#include <glm/vec2.hpp>

struct Colormaps;
struct FlatTextures;
struct Map;
struct SpriteTextures;
struct Video;
struct WallTextures;

/*
 * Casts one ray per framebuffer column with a grid DDA and draws the walls,
 * then fills the floor and ceiling around them as horizontal spans.
 * Projection accounts for the non-square pixels: the framebuffer is shown at
 * 4:3, so vertical focal length is scaled by the pixel aspect.
 */
struct Raycaster {
	int width;
	int height;		/* rows of the 3D view (framebuffer minus the status bar) */
	float planeLen;		/* tan(hfov / 2) */
	float focalX;		/* columns per unit of camera-plane x at distance 1 */
	float focalY;		/* rows per world unit at distance 1 */
	float *zbuffer;		/* perpendicular wall distance per column, for sprites */
	int *wallTop;		/* first wall row per column */
	int *wallBottom;	/* one past the last wall row per column */
};

/* Everything the renderer reads but does not own. */
struct RenderAssets {
	const WallTextures *walls;
	const FlatTextures *flats;
	const SpriteTextures *sprites;
	const Colormaps *colormaps;
};

bool raycaster_init(Raycaster *rc, const Video *video, float hfovDegrees, int viewHeight);
void raycaster_shutdown(Raycaster *rc);

/* Colormap level for something `distance` units in front of the camera. */
int raycaster_light_level(float distance);

/* Walls, then floor and ceiling. Fills zbuffer for the sprite pass. */
void raycaster_render(Raycaster *rc, Video *video, const Map *map,
	const RenderAssets *assets, glm::vec2 pos, float angle);

#endif
