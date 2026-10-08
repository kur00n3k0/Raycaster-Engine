#ifndef HUD_H
#define HUD_H

struct Game;
struct Raycaster;
struct RenderAssets;
struct Video;

enum {
	HUD_BAR_HEIGHT = 32	/* status bar rows at 320x200; the 3D view gets the rest */
};

/*
 * Everything drawn on top of the 3D view, straight into the framebuffer:
 * the first-person weapon (with walk bob and muzzle flash), the status bar
 * (health, kills, ammo), the message line and the death screen. Text uses a
 * built-in 5x7 bitmap font. Call after sprites_render.
 */
void hud_draw(Video *video, const Raycaster *rc, const Game *game, const RenderAssets *assets,
	const char *stats);	/* top-right line, or null */

#endif
