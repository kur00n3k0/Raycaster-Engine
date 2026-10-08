#ifndef SPRITES_H
#define SPRITES_H

#include <glm/vec2.hpp>

struct Entity;
struct Raycaster;
struct RenderAssets;
struct Video;

/*
 * Billboard sprites, Wolf3D style: every entity is a 1x1 unit image standing
 * on the floor, always facing the camera. Sorted far to near, each column is
 * clipped against the wall z-buffer from the raycaster pass.
 * Call after raycaster_render for the same camera.
 */
void sprites_render(const Raycaster *rc, Video *video, const RenderAssets *assets,
	const Entity *entities, int count, glm::vec2 pos, float angle);

#endif
