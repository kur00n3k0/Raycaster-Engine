#ifndef GAME_H
#define GAME_H

#include "Audio.h"
#include "Map.h"
#include "SpriteIds.h"

#include <glm/vec2.hpp>

enum {
	GAME_TICK_RATE = 70	/* Hz, Doom's 35 Hz doubled */
};

/* One tick's worth of input, sampled by main from the window. */
struct Input {
	bool forward;
	bool back;
	bool strafeLeft;
	bool strafeRight;
	bool turnLeft;
	bool turnRight;
	bool run;
	bool use;		/* open doors, push secret walls, restart when dead (on press) */
	bool fire;		/* held: fires whenever the weapon is ready */
};

struct Player {
	glm::vec2 pos;
	float angle;		/* radians, 0 = east, grows clockwise (towards +y) */
	int health;
	int ammo;
	bool dead;
	float fireCooldown;	/* seconds until the pistol can fire again */
	float flashTime;	/* seconds the muzzle-flash frame stays up */
	float damageFlash;	/* Doom's damagecount: + damage taken, fades 35 per second */
	float bonusFlash;	/* Doom's bonuscount: + 6 per pickup, fades 35 per second */
	float bobPhase;		/* grows with distance walked; drives the weapon bob */
};

enum EnemyState : uint8_t {
	ENEMY_IDLE,	/* standing guard until it sees or hears the player */
	ENEMY_CHASE,	/* walking towards the player */
	ENEMY_AIM,	/* stopped, about to fire */
	ENEMY_SHOOT,	/* muzzle-flash frame after firing */
	ENEMY_PAIN,	/* flinching after being hit */
	ENEMY_DEAD
};

/* Anything in the world that is drawn as a sprite. Spawned from map things. */
struct Entity {
	uint8_t type;		/* ThingType */
	uint8_t sprite;		/* SpriteId to draw this frame */
	uint8_t state;		/* EnemyState (enemies only) */
	bool active;		/* false once picked up: not drawn, not touched */
	bool solid;		/* blocks movement */
	glm::vec2 pos;
	int health;		/* enemies */
	int amount;		/* pickups: health or ammo given */
	float timer;		/* time left in the current enemy state */
	float cooldown;		/* enemies: time before the next attack */
	float anim;		/* walk animation clock */
};

enum {
	MAX_DROPS = 32,		/* ammo clips dropped by enemies */
	MAX_ENTITIES = MAP_MAX_THINGS + MAX_DROPS,
	MAX_SOUND_EVENTS = 32,
	MAP_PATH_MAX = 256
};

/*
 * Sounds requested by game logic during the ticks of one frame. main plays
 * and clears them; game code never touches OpenAL, so ticks stay
 * deterministic and testable without a sound device.
 */
struct SoundEvent {
	uint8_t sfx;		/* Sfx */
	glm::vec2 pos;
};

struct Game {
	Map map;
	char mapPath[MAP_PATH_MAX];	/* kept for restarting the level */
	Player player;
	Entity entities[MAX_ENTITIES];
	int entityCount;
	int enemiesTotal;
	int enemiesKilled;

	bool useHeld;		/* use was down last tick, for edge detection */
	uint32_t rng;		/* xorshift state: same seed and input, same game */

	/* Breadth-first steps from the player's cell through open cells and doors, -1 = unreachable. */
	int16_t *pathDist;
	int *pathQueue;

	const char *message;	/* HUD message line, null when none */
	float messageTime;

	SoundEvent sounds[MAX_SOUND_EVENTS];
	int soundCount;
};

bool game_init(Game *game, const char *mapPath);
void game_shutdown(Game *game);

/* Advance the world by exactly 1 / GAME_TICK_RATE seconds. */
void game_tick(Game *game, const Input *input);

/* Which flash palette to show (index into palette_build_flashes output, 0 = none). */
int game_flash_palette(const Game *game);

#endif
