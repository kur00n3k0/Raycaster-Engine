#ifndef SPRITEIDS_H
#define SPRITEIDS_H

#include <stdint.h>

/*
 * Every sprite image, shared by the game (which picks a frame for each
 * entity) and the loader (which loads assets/sprites/<name>.pcx in this
 * order, see SPRITE_NAMES in Textures.cpp).
 */
enum SpriteId : uint8_t {
	SPR_ENEMY_STAND,
	SPR_ENEMY_WALK1,
	SPR_ENEMY_WALK2,
	SPR_ENEMY_SHOOT,
	SPR_ENEMY_PAIN,
	SPR_ENEMY_DEAD,
	SPR_HEALTH,
	SPR_AMMO,
	SPR_BARREL,
	SPR_LAMP,
	SPR_WEAPON_IDLE,	/* first-person pistol, drawn by the HUD */
	SPR_WEAPON_FIRE,
	SPR_EXPLODE1,		/* barrel bursting (fuse) */
	SPR_EXPLODE2,		/* fireball */
	SPR_EXPLODE3,		/* fading fire and smoke */
	SPR_FISTS_IDLE,		/* first-person fists, drawn by the HUD */
	SPR_FISTS_PUNCH,
	SPR_SMG_IDLE,		/* first-person submachine gun */
	SPR_SMG_FIRE,
	SPR_SMG_PICKUP,		/* submachine gun lying on the floor (map thing 'm') */
	SPR_COUNT
};

/* Sprites that give off their own light: drawn without distance shading. */
static inline bool sprite_fullbright(uint8_t id)
{
	return id >= SPR_EXPLODE1 && id <= SPR_EXPLODE3;
}

#endif
