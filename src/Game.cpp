#include "Game.h"

#include "Palette.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glm/geometric.hpp>

static const float TICK = 1.0f / (float)GAME_TICK_RATE;
static const float TWO_PI = 6.28318530718f;

/* Player */
static const float WALK_SPEED = 3.0f;		/* tiles per second */
static const float RUN_FACTOR = 1.8f;
static const float TURN_SPEED = 2.6f;		/* radians per second */
static const float PLAYER_RADIUS = 0.25f;	/* keeps the camera off the walls */
static const int START_HEALTH = 100;
static const int MAX_HEALTH = 100;
static const int START_AMMO = 12;
static const int MAX_AMMO = 99;
static const float FIRE_COOLDOWN = 0.4f;	/* seconds between pistol shots */
static const float FLASH_TIME = 0.12f;
static const float FLASH_FADE = 35.0f;		/* flash counters lose this much per second (1 per Doom tic) */
static const float FLASH_MAX = 100.0f;
static const float BONUS_ADD = 6.0f;		/* Doom's BONUSADD */
static const float HITSCAN_WIDTH = 0.3f;	/* half-width of an enemy for bullets */
static const float MESSAGE_TIME = 3.0f;

/* Things */
static const float THING_RADIUS = 0.3f;		/* solid things: enemies, barrels */
static const float PICKUP_RADIUS = 0.55f;
static const int HEALTH_PICKUP = 25;
static const int AMMO_PICKUP = 8;
static const int AMMO_DROP = 4;

/* Barrels: Doom's 128-damage blast, falling off linearly to nothing at the radius. */
static const float BARREL_FUSE_TIME = 0.3f;	/* Doom: 15 tics from death to A_Explode */
static const float BARREL_BLAST_TIME = 0.15f;
static const float BARREL_SMOKE_TIME = 0.25f;
static const float BLAST_RADIUS = 3.0f;		/* tiles from the barrel to the victim's edge */
static const int BLAST_DAMAGE = 150;		/* point blank: kills a guard or a full-health player */
static const float BLAST_HEARING = 12.0f;	/* idle guards this close wake up */

/* Enemies */
static const int ENEMY_HEALTH = 25;
static const float ENEMY_SPEED = 1.5f;		/* tiles per second */
static const float ENEMY_SIGHT = 14.0f;		/* tiles */
static const int ENEMY_HEARING = 14;		/* path steps a gunshot carries */
static const float ENEMY_ATTACK_RANGE = 10.0f;
static const float ENEMY_KEEP_AWAY = 1.5f;	/* stops closing in at this distance */
static const float ENEMY_AIM_TIME = 0.35f;
static const float ENEMY_SHOOT_TIME = 0.15f;
static const float ENEMY_PAIN_TIME = 0.3f;
static const float ENEMY_WALK_FRAME = 0.2f;

/* Map objects */
static const float DOOR_SPEED = 1.6f;		/* fraction of the door per second */
static const float DOOR_OPEN_TIME = 4.0f;	/* seconds before an open door closes */
static const float DOOR_RETRY_TIME = 0.5f;	/* wait again if something is in the doorway */
static const float DOOR_SEE_THROUGH = 0.5f;	/* doors open at least this far pass sight and bullets */
static const float PUSHWALL_SPEED = 1.0f;	/* cells per second */
static const int PUSHWALL_DISTANCE = 2;		/* cells a push wall travels */

static const uint32_t RNG_SEED = 0x9E3779B9u;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static uint32_t random_u32(Game *game)
{
	uint32_t s = game->rng;
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	game->rng = s;
	return s;
}

/* Uniform in [lo, hi]. */
static int random_int(Game *game, int lo, int hi)
{
	return lo + (int)(random_u32(game) % (uint32_t)(hi - lo + 1));
}

/* Uniform in [0, 1). */
static float random_float(Game *game)
{
	return (float)(random_u32(game) >> 8) / 16777216.0f;
}

/* Queue a sound for main to play; extra ones in a busy frame are dropped. */
static void emit_sound(Game *game, int sfx, glm::vec2 pos)
{
	if (game->soundCount == MAX_SOUND_EVENTS)
		return;
	SoundEvent *e = &game->sounds[game->soundCount++];
	e->sfx = (uint8_t)sfx;
	e->pos = pos;
}

static void show_message(Game *game, const char *text)
{
	game->message = text;
	game->messageTime = MESSAGE_TIME;
}

static glm::vec2 cell_centre(int x, int y)
{
	return glm::vec2((float)x + 0.5f, (float)y + 0.5f);
}

static int cell_of(float v)
{
	return (int)floorf(v);
}

/* Does a box of half-size r at pos overlap cell (x, y)? */
static bool box_in_cell(glm::vec2 pos, float r, int x, int y)
{
	return pos.x + r > (float)x && pos.x - r < (float)(x + 1)
		&& pos.y + r > (float)y && pos.y - r < (float)(y + 1);
}

static bool is_enemy(const Entity *e)
{
	return e->type == THING_ENEMY;
}

static bool is_alive_enemy(const Entity *e)
{
	return e->active && is_enemy(e) && e->state != ENEMY_DEAD;
}

/* Is anything that should hold a door open (player, solid thing) in cell (x, y)? */
static bool cell_occupied(const Game *game, int x, int y)
{
	if (!game->player.dead && box_in_cell(game->player.pos, PLAYER_RADIUS, x, y))
		return true;
	for (int i = 0; i < game->entityCount; i++) {
		const Entity *e = &game->entities[i];
		if (e->active && e->solid && box_in_cell(e->pos, THING_RADIUS, x, y))
			return true;
	}
	return false;
}

/* ------------------------------------------------------------------------- */
/* Collision                                                                 */
/* ------------------------------------------------------------------------- */

static bool box_hits_map(const Map *map, glm::vec2 pos, float r)
{
	int x0 = cell_of(pos.x - r), x1 = cell_of(pos.x + r);
	int y0 = cell_of(pos.y - r), y1 = cell_of(pos.y + r);
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			if (map_blocks(map, x, y))
				return true;
		}
	}
	return false;
}

/*
 * Would moving the mover (entity index `self`, or -1 for the player) from
 * `from` to `to` push it into another solid body? Already-overlapping bodies
 * may still move apart, so nobody gets stuck inside a dropped corpse or
 * a spawn on top of something.
 */
static bool blocked_by_bodies(const Game *game, int self, float r, glm::vec2 from, glm::vec2 to)
{
	for (int i = -1; i < game->entityCount; i++) {
		if (i == self)
			continue;
		glm::vec2 other;
		float otherR;
		if (i == -1) {
			if (game->player.dead)
				continue;
			other = game->player.pos;
			otherR = PLAYER_RADIUS;
		} else {
			const Entity *e = &game->entities[i];
			if (!e->active || !e->solid)
				continue;
			other = e->pos;
			otherR = THING_RADIUS;
		}
		float reach = r + otherR;
		glm::vec2 dTo = to - other;
		if (fabsf(dTo.x) >= reach || fabsf(dTo.y) >= reach)
			continue;
		glm::vec2 dFrom = from - other;
		bool wasOverlapping = fabsf(dFrom.x) < reach && fabsf(dFrom.y) < reach;
		if (!wasOverlapping || glm::length(dTo) < glm::length(dFrom))
			return true;
	}
	return false;
}

/* Move one axis at a time so bodies slide along walls and each other. Returns distance moved. */
static float try_move(Game *game, int self, glm::vec2 *pos, float r, glm::vec2 delta)
{
	glm::vec2 start = *pos;

	glm::vec2 next = *pos;
	next.x += delta.x;
	if (!box_hits_map(&game->map, next, r) && !blocked_by_bodies(game, self, r, *pos, next))
		*pos = next;

	next = *pos;
	next.y += delta.y;
	if (!box_hits_map(&game->map, next, r) && !blocked_by_bodies(game, self, r, *pos, next))
		*pos = next;

	return glm::length(*pos - start);
}

/* ------------------------------------------------------------------------- */
/* Sight and bullets                                                         */
/* ------------------------------------------------------------------------- */

/* Do sight and bullets pass through cell (x, y)? */
static bool cell_transparent(const Map *map, int x, int y)
{
	uint8_t tile = map_tile(map, x, y);
	if (tile == TILE_EMPTY)
		return true;
	if (tile == TILE_DOOR)
		return map_door(map, x, y)->open >= DOOR_SEE_THROUGH;
	return false;
}

/*
 * Grid DDA from `from` along `dir` (need not be unit length): distance in
 * units of |dir| to the first opaque cell, or maxDist if none before it.
 */
static float trace_distance(const Map *map, glm::vec2 from, glm::vec2 dir, float maxDist)
{
	int x = cell_of(from.x), y = cell_of(from.y);
	float deltaX = dir.x != 0.0f ? fabsf(1.0f / dir.x) : 1e30f;
	float deltaY = dir.y != 0.0f ? fabsf(1.0f / dir.y) : 1e30f;
	int stepX = dir.x < 0.0f ? -1 : 1;
	int stepY = dir.y < 0.0f ? -1 : 1;
	float sideX = (dir.x < 0.0f ? from.x - (float)x : (float)x + 1.0f - from.x) * deltaX;
	float sideY = (dir.y < 0.0f ? from.y - (float)y : (float)y + 1.0f - from.y) * deltaY;

	for (;;) {
		float t;
		if (sideX < sideY) {
			t = sideX;
			sideX += deltaX;
			x += stepX;
		} else {
			t = sideY;
			sideY += deltaY;
			y += stepY;
		}
		if (t >= maxDist)
			return maxDist;
		if (!cell_transparent(map, x, y))
			return t;
	}
}

static bool line_of_sight(const Game *game, glm::vec2 from, glm::vec2 to)
{
	glm::vec2 d = to - from;
	float dist = glm::length(d);
	if (dist < 1e-4f)
		return true;
	return trace_distance(&game->map, from, d / dist, dist) >= dist;
}

/* ------------------------------------------------------------------------- */
/* Pathfinding: breadth-first distance field from the player                 */
/* ------------------------------------------------------------------------- */

static bool cell_walkable(const Map *map, int x, int y)
{
	uint8_t tile = map_tile(map, x, y);
	return tile == TILE_EMPTY || tile == TILE_DOOR;	/* enemies open doors */
}

static void update_paths(Game *game)
{
	const Map *map = &game->map;
	const int w = map->width;
	memset(game->pathDist, 0xFF, sizeof(int16_t) * (size_t)(w * map->height));

	int px = cell_of(game->player.pos.x), py = cell_of(game->player.pos.y);
	if (px < 0 || py < 0 || px >= w || py >= map->height)
		return;
	int head = 0, tail = 0;
	game->pathDist[py * w + px] = 0;
	game->pathQueue[tail++] = py * w + px;

	static const int DX[4] = { 1, -1, 0, 0 };
	static const int DY[4] = { 0, 0, 1, -1 };
	while (head < tail) {
		int cell = game->pathQueue[head++];
		int cx = cell % w, cy = cell / w;
		for (int d = 0; d < 4; d++) {
			int nx = cx + DX[d], ny = cy + DY[d];
			if (!cell_walkable(map, nx, ny) || game->pathDist[ny * w + nx] >= 0)
				continue;
			game->pathDist[ny * w + nx] = (int16_t)(game->pathDist[cell] + 1);
			game->pathQueue[tail++] = ny * w + nx;
		}
	}
}

static int path_dist(const Game *game, int x, int y)
{
	if (x < 0 || y < 0 || x >= game->map.width || y >= game->map.height)
		return -1;
	return game->pathDist[y * game->map.width + x];
}

/* ------------------------------------------------------------------------- */
/* Doors                                                                     */
/* ------------------------------------------------------------------------- */

static void open_door(Game *game, Door *d)
{
	if (d->state == DOOR_CLOSED || d->state == DOOR_CLOSING) {
		d->state = DOOR_OPENING;
		emit_sound(game, SFX_DOOR_OPEN, cell_centre(d->x, d->y));
	}
}

static void use_door(Game *game, Door *d)
{
	switch (d->state) {
	case DOOR_CLOSED:
	case DOOR_CLOSING:
		open_door(game, d);
		break;
	case DOOR_OPEN:
	case DOOR_OPENING:
		if (!cell_occupied(game, d->x, d->y)) {
			d->state = DOOR_CLOSING;
			emit_sound(game, SFX_DOOR_CLOSE, cell_centre(d->x, d->y));
		}
		break;
	}
}

static void update_doors(Game *game)
{
	for (int i = 0; i < game->map.doorCount; i++) {
		Door *d = &game->map.doors[i];
		switch (d->state) {
		case DOOR_OPENING:
			d->open += DOOR_SPEED * TICK;
			if (d->open >= 1.0f) {
				d->open = 1.0f;
				d->state = DOOR_OPEN;
				d->timer = DOOR_OPEN_TIME;
			}
			break;
		case DOOR_OPEN:
			d->timer -= TICK;
			if (d->timer <= 0.0f) {
				if (cell_occupied(game, d->x, d->y)) {
					d->timer = DOOR_RETRY_TIME;
				} else {
					d->state = DOOR_CLOSING;
					emit_sound(game, SFX_DOOR_CLOSE, cell_centre(d->x, d->y));
				}
			}
			break;
		case DOOR_CLOSING:
			/* Only reachable with the doorway empty; never close on anyone. */
			d->open -= DOOR_SPEED * TICK;
			if (d->open <= 0.0f) {
				d->open = 0.0f;
				d->state = DOOR_CLOSED;
			}
			break;
		default:
			break;
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Push walls                                                                */
/* ------------------------------------------------------------------------- */

static void set_tile(Map *map, int x, int y, uint8_t tile)
{
	map->tiles[y * map->width + x] = tile;
}

/* Can the push wall slide into cell (x, y)? Not over the player or any thing. */
static bool pushwall_can_enter(const Game *game, int x, int y)
{
	if (map_tile(&game->map, x, y) != TILE_EMPTY)
		return false;
	if (box_in_cell(game->player.pos, PLAYER_RADIUS, x, y))
		return false;
	for (int i = 0; i < game->entityCount; i++) {
		const Entity *e = &game->entities[i];
		if (e->active && cell_of(e->pos.x) == x && cell_of(e->pos.y) == y)
			return false;
	}
	return true;
}

static void push_secret(Game *game, int x, int y, int dx, int dy)
{
	Pushwall *pw = &game->map.pushwall;
	if (pw->active || !pushwall_can_enter(game, x + dx, y + dy))
		return;
	pw->active = true;
	pw->x = x;
	pw->y = y;
	pw->dx = dx;
	pw->dy = dy;
	pw->offset = 0.0f;
	pw->moved = 0;
	set_tile(&game->map, x, y, TILE_PUSHWALL);
	set_tile(&game->map, x + dx, y + dy, TILE_PUSHWALL);
	game->secretsFound++;
	emit_sound(game, SFX_PUSHWALL, cell_centre(x, y));
	show_message(game, "YOU FOUND A SECRET!");
}

static void update_pushwall(Game *game)
{
	Pushwall *pw = &game->map.pushwall;
	if (!pw->active)
		return;

	pw->offset += PUSHWALL_SPEED * TICK;
	if (pw->offset < 1.0f)
		return;

	/* Fully inside the next cell. */
	set_tile(&game->map, pw->x, pw->y, TILE_EMPTY);
	pw->x += pw->dx;
	pw->y += pw->dy;
	pw->moved++;
	pw->offset = 0.0f;

	int nx = pw->x + pw->dx;
	int ny = pw->y + pw->dy;
	if (pw->moved >= PUSHWALL_DISTANCE || !pushwall_can_enter(game, nx, ny)) {
		/* Comes to rest as an ordinary wall. */
		set_tile(&game->map, pw->x, pw->y, TILE_WALL_FIRST);
		pw->active = false;
		return;
	}
	set_tile(&game->map, nx, ny, TILE_PUSHWALL);
}

/* ------------------------------------------------------------------------- */
/* Damage                                                                    */
/* ------------------------------------------------------------------------- */

static void hurt_player(Game *game, int damage)
{
	Player *p = &game->player;
	if (p->dead)
		return;
	p->health -= damage;
	p->damageFlash += (float)damage;
	if (p->damageFlash > FLASH_MAX)
		p->damageFlash = FLASH_MAX;
	emit_sound(game, SFX_PLAYER_HURT, p->pos);
	if (p->health <= 0) {
		p->health = 0;
		p->dead = true;
		show_message(game, nullptr);
	}
}

static void alert_enemy(Game *game, Entity *e)
{
	if (e->state != ENEMY_IDLE)
		return;
	e->state = ENEMY_CHASE;
	e->cooldown = 0.4f + 0.4f * random_float(game);	/* reaction time before the first shot */
	emit_sound(game, SFX_ENEMY_ALERT, e->pos);
}

static Entity *spawn_drop(Game *game, uint8_t type, uint8_t sprite, glm::vec2 pos, int amount)
{
	if (game->entityCount == MAX_ENTITIES)
		return nullptr;
	Entity *e = &game->entities[game->entityCount++];
	memset(e, 0, sizeof(*e));
	e->type = type;
	e->sprite = sprite;
	e->active = true;
	e->pos = pos;
	e->amount = amount;
	return e;
}

static void hurt_enemy(Game *game, Entity *e, int damage)
{
	e->health -= damage;
	if (e->health <= 0) {
		e->state = ENEMY_DEAD;
		e->sprite = SPR_ENEMY_DEAD;
		e->solid = false;
		emit_sound(game, SFX_ENEMY_DEATH, e->pos);
		game->enemiesKilled++;
		/* Wolf3D guards drop their clip. Nudged towards the player so it is not hidden by the corpse. */
		glm::vec2 toPlayer = game->player.pos - e->pos;
		float len = glm::length(toPlayer);
		glm::vec2 dropPos = len > 0.0f ? e->pos + toPlayer * (0.2f / len) : e->pos;
		spawn_drop(game, THING_AMMO, SPR_AMMO, dropPos, AMMO_DROP);
		if (game->enemiesKilled == game->enemiesTotal)
			show_message(game, "ALL ENEMIES DOWN!");
		return;
	}
	bool wasIdle = e->state == ENEMY_IDLE;
	e->state = ENEMY_PAIN;
	e->sprite = SPR_ENEMY_PAIN;
	e->timer = ENEMY_PAIN_TIME;
	if (wasIdle)
		e->cooldown = 0.3f;
	emit_sound(game, SFX_ENEMY_PAIN, e->pos);
}

/* ------------------------------------------------------------------------- */
/* Explosive barrels                                                         */
/* ------------------------------------------------------------------------- */

/* A bullet or a nearby blast lights the fuse. Only once: a burning barrel cannot be set off again. */
static void ignite_barrel(Entity *e)
{
	if (!e->active || e->type != THING_BARREL || e->state != BARREL_IDLE)
		return;
	e->state = BARREL_FUSE;
	e->timer = BARREL_FUSE_TIME;
	e->sprite = SPR_EXPLODE1;
}

/*
 * Blast damage at distance d from the centre to the victim's edge (Doom's
 * P_RadiusAttack subtracts the radius too). Walls and closed doors shield.
 */
static int blast_damage(const Game *game, glm::vec2 centre, glm::vec2 pos, float radius)
{
	float d = glm::length(pos - centre) - radius;
	if (d < 0.0f)
		d = 0.0f;
	if (d >= BLAST_RADIUS || !line_of_sight(game, centre, pos))
		return 0;
	return (int)((float)BLAST_DAMAGE * (1.0f - d / BLAST_RADIUS));
}

static void explode_barrel(Game *game, Entity *barrel)
{
	glm::vec2 centre = barrel->pos;
	barrel->state = BARREL_BLAST;
	barrel->timer = BARREL_BLAST_TIME;
	barrel->sprite = SPR_EXPLODE2;
	barrel->solid = false;
	emit_sound(game, SFX_EXPLODE, centre);

	for (int i = 0; i < game->entityCount; i++) {
		Entity *e = &game->entities[i];
		if (e == barrel || !e->active)
			continue;
		if (is_alive_enemy(e)) {
			int damage = blast_damage(game, centre, e->pos, THING_RADIUS);
			if (damage > 0) {
				hurt_enemy(game, e, damage);
				continue;
			}
			/* Out of reach but heard it. */
			if (e->state == ENEMY_IDLE && glm::length(e->pos - centre) < BLAST_HEARING)
				alert_enemy(game, e);
		} else if (e->type == THING_BARREL && blast_damage(game, centre, e->pos, THING_RADIUS) > 0) {
			ignite_barrel(e);		/* chain reaction, one fuse later */
		}
	}
	if (!game->player.dead) {
		int damage = blast_damage(game, centre, game->player.pos, PLAYER_RADIUS);
		if (damage > 0)
			hurt_player(game, damage);
	}
}

static void update_barrel(Game *game, Entity *e)
{
	if (e->state == BARREL_IDLE)
		return;
	e->timer -= TICK;
	if (e->timer > 0.0f)
		return;
	switch (e->state) {
	case BARREL_FUSE:
		explode_barrel(game, e);
		break;
	case BARREL_BLAST:
		e->state = BARREL_SMOKE;
		e->timer = BARREL_SMOKE_TIME;
		e->sprite = SPR_EXPLODE3;
		break;
	default:
		e->active = false;	/* nothing left, like Doom */
		break;
	}
}

/* ------------------------------------------------------------------------- */
/* Player actions                                                            */
/* ------------------------------------------------------------------------- */

/*
 * Use acts on the cell directly ahead along the dominant axis of the view,
 * like Wolf3D, and pushes in that cardinal direction.
 */
/* Exit used: freeze the level and show the intermission (or the end of the episode). */
static void complete_level(Game *game)
{
	game->phase = game->map.next[0] ? PHASE_INTERMISSION : PHASE_FINISHED;
	game->phaseTime = 0.0f;
	game->message = nullptr;
	emit_sound(game, SFX_EXIT, game->player.pos);
}

static void player_use(Game *game)
{
	const Player *p = &game->player;
	float fx = cosf(p->angle);
	float fy = sinf(p->angle);
	int dx = 0, dy = 0;
	if (fabsf(fx) >= fabsf(fy))
		dx = fx > 0.0f ? 1 : -1;
	else
		dy = fy > 0.0f ? 1 : -1;

	int x = cell_of(p->pos.x) + dx;
	int y = cell_of(p->pos.y) + dy;
	uint8_t tile = map_tile(&game->map, x, y);
	if (tile == TILE_DOOR)
		use_door(game, map_door(&game->map, x, y));
	else if (tile == TILE_SECRET)
		push_secret(game, x, y, dx, dy);
	else if (tile == TILE_EXIT)
		complete_level(game);
}

/* Things a bullet can hit: living enemies and barrels that have not gone off yet. */
static bool is_shootable(const Entity *e)
{
	return is_alive_enemy(e) || (e->active && e->type == THING_BARREL && e->state == BARREL_IDLE);
}

/* Hitscan along the view direction: the nearest enemy or barrel the ray passes close enough to. */
static void player_fire(Game *game)
{
	Player *p = &game->player;
	if (p->ammo == 0) {
		show_message(game, "OUT OF AMMO");
		return;
	}
	p->ammo--;
	p->fireCooldown = FIRE_COOLDOWN;
	p->flashTime = FLASH_TIME;
	emit_sound(game, SFX_PISTOL, p->pos);

	glm::vec2 dir(cosf(p->angle), sinf(p->angle));
	float wallDist = trace_distance(&game->map, p->pos, dir, 64.0f);

	Entity *target = nullptr;
	float best = wallDist;
	for (int i = 0; i < game->entityCount; i++) {
		Entity *e = &game->entities[i];
		if (!is_shootable(e))
			continue;
		glm::vec2 rel = e->pos - p->pos;
		float along = rel.x * dir.x + rel.y * dir.y;
		float lateral = fabsf(rel.x * dir.y - rel.y * dir.x);
		if (along > 0.0f && along < best && lateral < HITSCAN_WIDTH) {
			best = along;
			target = e;
		}
	}
	if (target && target->type == THING_BARREL) {
		ignite_barrel(target);
	} else if (target) {
		/* Point blank hurts more, like Wolf3D's distance-scaled damage. */
		int damage = random_int(game, 8, 16) + (best < 2.0f ? 6 : 0);
		hurt_enemy(game, target, damage);
	}

	/* The shot is heard by every idle enemy within earshot along open paths. */
	for (int i = 0; i < game->entityCount; i++) {
		Entity *e = &game->entities[i];
		if (!is_alive_enemy(e) || e->state != ENEMY_IDLE)
			continue;
		int d = path_dist(game, cell_of(e->pos.x), cell_of(e->pos.y));
		if (d >= 0 && d <= ENEMY_HEARING)
			alert_enemy(game, e);
	}
}

static void pick_up_items(Game *game)
{
	Player *p = &game->player;
	for (int i = 0; i < game->entityCount; i++) {
		Entity *e = &game->entities[i];
		if (!e->active || (e->type != THING_HEALTH && e->type != THING_AMMO))
			continue;
		glm::vec2 d = e->pos - p->pos;
		if (fabsf(d.x) > PICKUP_RADIUS || fabsf(d.y) > PICKUP_RADIUS)
			continue;

		if (e->type == THING_HEALTH) {
			if (p->health >= MAX_HEALTH)
				continue;	/* leave it for later, like Wolf3D */
			p->health = p->health + e->amount > MAX_HEALTH ? MAX_HEALTH : p->health + e->amount;
			show_message(game, "PICKED UP A MEDKIT");
		} else {
			if (p->ammo >= MAX_AMMO)
				continue;
			p->ammo = p->ammo + e->amount > MAX_AMMO ? MAX_AMMO : p->ammo + e->amount;
			show_message(game, "PICKED UP AMMO");
		}
		e->active = false;
		if (i < game->mapEntities)
			game->itemsTaken++;
		p->bonusFlash += BONUS_ADD;
		emit_sound(game, SFX_PICKUP, e->pos);
	}
}

/* Muzzle flash and palette flashes wind down every tick, whatever else is going on. */
static void fade_player_flashes(Player *p)
{
	p->fireCooldown -= TICK;
	p->flashTime -= TICK;
	p->damageFlash = p->damageFlash > FLASH_FADE * TICK ? p->damageFlash - FLASH_FADE * TICK : 0.0f;
	p->bonusFlash = p->bonusFlash > FLASH_FADE * TICK ? p->bonusFlash - FLASH_FADE * TICK : 0.0f;
}

static void update_player(Game *game, const Input *input)
{
	Player *p = &game->player;
	fade_player_flashes(p);
	if (p->dead)
		return;

	float turn = 0.0f;
	if (input->turnLeft)  turn -= 1.0f;
	if (input->turnRight) turn += 1.0f;
	p->angle += turn * TURN_SPEED * TICK;
	if (p->angle < 0.0f)    p->angle += TWO_PI;
	if (p->angle >= TWO_PI) p->angle -= TWO_PI;

	glm::vec2 forward(cosf(p->angle), sinf(p->angle));
	glm::vec2 right(-forward.y, forward.x);

	glm::vec2 wish(0.0f);
	if (input->forward)     wish += forward;
	if (input->back)        wish -= forward;
	if (input->strafeRight) wish += right;
	if (input->strafeLeft)  wish -= right;

	float len = glm::length(wish);
	if (len > 0.0f) {
		float speed = WALK_SPEED * (input->run ? RUN_FACTOR : 1.0f);
		float moved = try_move(game, -1, &p->pos, PLAYER_RADIUS, wish * (speed * TICK / len));
		p->bobPhase += moved * 4.0f;
	}

	if (input->fire && p->fireCooldown <= 0.0f)
		player_fire(game);

	pick_up_items(game);
}

/* ------------------------------------------------------------------------- */
/* Enemies                                                                   */
/* ------------------------------------------------------------------------- */

/*
 * Where to walk next: straight at the player when in view, otherwise
 * downhill on the path field to the centre of the neighbouring cell that is
 * one step closer. Enemies open closed doors in their way and wait.
 */
static bool enemy_step_target(Game *game, Entity *e, bool seesPlayer, glm::vec2 *target)
{
	if (seesPlayer) {
		*target = game->player.pos;
		return true;
	}
	int ex = cell_of(e->pos.x), ey = cell_of(e->pos.y);
	int here = path_dist(game, ex, ey);
	if (here <= 0)
		return false;

	static const int DX[4] = { 1, -1, 0, 0 };
	static const int DY[4] = { 0, 0, 1, -1 };
	for (int d = 0; d < 4; d++) {
		int nx = ex + DX[d], ny = ey + DY[d];
		if (path_dist(game, nx, ny) != here - 1)
			continue;
		if (map_tile(&game->map, nx, ny) == TILE_DOOR) {
			Door *door = map_door(&game->map, nx, ny);
			if (door->open < 1.0f) {
				open_door(game, door);
				return false;
			}
		}
		*target = cell_centre(nx, ny);
		return true;
	}
	return false;
}

static void update_enemy(Game *game, int index)
{
	Entity *e = &game->entities[index];
	Player *p = &game->player;
	glm::vec2 toPlayer = p->pos - e->pos;
	float dist = glm::length(toPlayer);
	bool seesPlayer = !p->dead && dist < ENEMY_SIGHT && line_of_sight(game, e->pos, p->pos);

	switch (e->state) {
	case ENEMY_IDLE:
		e->sprite = SPR_ENEMY_STAND;
		if (seesPlayer)
			alert_enemy(game, e);
		break;

	case ENEMY_CHASE: {
		e->cooldown -= TICK;
		if (seesPlayer && e->cooldown <= 0.0f && dist < ENEMY_ATTACK_RANGE) {
			e->state = ENEMY_AIM;
			e->timer = ENEMY_AIM_TIME;
			e->sprite = SPR_ENEMY_STAND;
			break;
		}
		glm::vec2 target;
		bool walk = !p->dead && enemy_step_target(game, e, seesPlayer, &target)
			&& !(seesPlayer && dist < ENEMY_KEEP_AWAY);
		float moved = 0.0f;
		if (walk) {
			glm::vec2 d = target - e->pos;
			float len = glm::length(d);
			if (len > 1e-3f) {
				float step = ENEMY_SPEED * TICK;
				if (step > len)
					step = len;
				moved = try_move(game, index, &e->pos, THING_RADIUS, d * (step / len));
			}
		}
		if (moved > 0.0f) {
			e->anim += TICK;
			e->sprite = ((int)(e->anim / ENEMY_WALK_FRAME) & 1) ? SPR_ENEMY_WALK2 : SPR_ENEMY_WALK1;
		} else {
			e->sprite = SPR_ENEMY_STAND;
		}
		break;
	}

	case ENEMY_AIM:
		e->timer -= TICK;
		if (e->timer > 0.0f)
			break;
		emit_sound(game, SFX_ENEMY_SHOT, e->pos);
		if (seesPlayer) {
			/* Better aim up close; Wolf3D-style distance-based hit chance. */
			float chance = 1.0f - dist / 12.0f;
			if (chance < 0.2f) chance = 0.2f;
			if (chance > 0.85f) chance = 0.85f;
			if (random_float(game) < chance)
				hurt_player(game, random_int(game, 4, 10) + (dist < 2.0f ? 5 : 0));
		}
		e->state = ENEMY_SHOOT;
		e->timer = ENEMY_SHOOT_TIME;
		e->sprite = SPR_ENEMY_SHOOT;
		e->cooldown = 0.9f + 0.9f * random_float(game);
		break;

	case ENEMY_SHOOT:
	case ENEMY_PAIN:
		e->timer -= TICK;
		if (e->timer <= 0.0f) {
			e->state = ENEMY_CHASE;
			e->sprite = SPR_ENEMY_STAND;
		}
		break;

	default:
		break;
	}
}

/* ------------------------------------------------------------------------- */
/* Setup and tick                                                            */
/* ------------------------------------------------------------------------- */

static void spawn_things(Game *game)
{
	static const uint8_t SPRITE_FOR_THING[THING_TYPE_COUNT] = {
		SPR_ENEMY_STAND, SPR_HEALTH, SPR_AMMO, SPR_BARREL, SPR_LAMP
	};

	game->entityCount = 0;
	game->enemiesTotal = 0;
	for (int i = 0; i < game->map.thingCount; i++) {
		const Thing *t = &game->map.things[i];
		Entity *e = &game->entities[game->entityCount++];
		memset(e, 0, sizeof(*e));
		e->type = t->type;
		e->sprite = SPRITE_FOR_THING[t->type];
		e->active = true;
		e->pos = t->pos;
		switch (t->type) {
		case THING_ENEMY:
			e->solid = true;
			e->health = ENEMY_HEALTH;
			e->state = ENEMY_IDLE;
			game->enemiesTotal++;
			break;
		case THING_BARREL:
			e->solid = true;
			break;
		case THING_HEALTH:
			e->amount = HEALTH_PICKUP;
			game->itemsTotal++;
			break;
		case THING_AMMO:
			e->amount = AMMO_PICKUP;
			game->itemsTotal++;
			break;
		default:
			break;
		}
	}
}

/* Fresh level state for mapPath. Keeps nothing but what the caller restores afterwards. */
static bool load_level(Game *game, const char *mapPath)
{
	memset(game, 0, sizeof(*game));
	snprintf(game->mapPath, sizeof(game->mapPath), "%s", mapPath);
	if (!map_load(&game->map, mapPath))
		return false;

	size_t cells = (size_t)game->map.width * (size_t)game->map.height;
	game->pathDist = (int16_t *)malloc(sizeof(int16_t) * cells);
	game->pathQueue = (int *)malloc(sizeof(int) * cells);

	Player *p = &game->player;
	p->pos = game->map.playerStart;
	p->angle = game->map.playerAngle;
	p->health = START_HEALTH;
	p->ammo = START_AMMO;

	game->rng = RNG_SEED;
	spawn_things(game);
	game->mapEntities = game->entityCount;
	for (int i = 0; i < game->map.width * game->map.height; i++) {
		if (game->map.tiles[i] == TILE_SECRET)
			game->secretsTotal++;
	}
	update_paths(game);
	return true;
}

bool game_init(Game *game, const char *mapPath)
{
	if (!load_level(game, mapPath))
		return false;
	snprintf(game->firstMapPath, sizeof(game->firstMapPath), "%s", mapPath);
	return true;
}

/* load_level, keeping the episode start. Exits if the map cannot be loaded. */
static void reload(Game *game, const char *path)
{
	char first[MAP_PATH_MAX];
	snprintf(first, sizeof(first), "%s", game->firstMapPath);
	game_shutdown(game);
	if (!load_level(game, path)) {
		fprintf(stderr, "Cannot load %s\n", path);
		exit(1);
	}
	snprintf(game->firstMapPath, sizeof(game->firstMapPath), "%s", first);
	game->useHeld = true;	/* the press that got us here must not also open a door */
}

/* @next is relative to the current map's directory. */
static void next_map_path(const Game *game, char *out, size_t size)
{
	const char *slash = strrchr(game->mapPath, '/');
	int dirLen = slash ? (int)(slash - game->mapPath + 1) : 0;
	snprintf(out, size, "%.*s%s", dirLen, game->mapPath, game->map.next);
}

/* Intermission over: next map, carrying health and ammo over like Wolf3D. */
static void next_level(Game *game)
{
	char path[MAP_PATH_MAX];
	next_map_path(game, path, sizeof(path));
	int health = game->player.health;
	int ammo = game->player.ammo;
	reload(game, path);
	game->player.health = health;
	game->player.ammo = ammo;
	game->levelChanged = true;
}

void game_shutdown(Game *game)
{
	map_free(&game->map);
	free(game->pathDist);
	free(game->pathQueue);
	game->pathDist = nullptr;
	game->pathQueue = nullptr;
}

/* Reload the level from scratch after death. */
static void restart(Game *game)
{
	char path[MAP_PATH_MAX];
	snprintf(path, sizeof(path), "%s", game->mapPath);
	reload(game, path);
}

/* After the last map: the whole episode again, from the first map with fresh stats. */
static void restart_episode(Game *game)
{
	char path[MAP_PATH_MAX];
	snprintf(path, sizeof(path), "%s", game->firstMapPath);
	reload(game, path);
	game->levelChanged = true;
}

void game_tick(Game *game, const Input *input)
{
	bool usePressed = input->use && !game->useHeld;
	game->useHeld = input->use;

	/* Between levels the world is frozen; only the screen's own timer and the flashes run. */
	if (game->phase != PHASE_PLAYING) {
		fade_player_flashes(&game->player);
		game->phaseTime += TICK;
		if (usePressed && game->phaseTime >= GAME_INTERMISSION_DELAY) {
			if (game->phase == PHASE_INTERMISSION)
				next_level(game);
			else
				restart_episode(game);
		}
		return;
	}
	if (!game->player.dead)
		game->levelTicks++;

	if (game->player.dead && usePressed) {
		restart(game);
		return;
	}
	if (usePressed && !game->player.dead)
		player_use(game);

	update_doors(game);
	update_pushwall(game);
	update_player(game, input);
	update_paths(game);

	for (int i = 0; i < game->entityCount; i++) {
		Entity *e = &game->entities[i];
		if (is_alive_enemy(e))
			update_enemy(game, i);
		else if (e->active && e->type == THING_BARREL)
			update_barrel(game, e);
	}

	if (game->messageTime > 0.0f) {
		game->messageTime -= TICK;
		if (game->messageTime <= 0.0f)
			game->message = nullptr;
	}
}

/* Doom's ST_doPaletteStuff: damage wins over pickups, (count + 7) / 8 palettes deep. */
int game_flash_palette(const Game *game)
{
	int damage = (int)ceilf(game->player.damageFlash);
	if (damage > 0) {
		int level = (damage + 7) >> 3;
		if (level > PAL_FLASH_RED_COUNT)
			level = PAL_FLASH_RED_COUNT;
		return PAL_FLASH_RED_FIRST + level - 1;
	}
	int bonus = (int)ceilf(game->player.bonusFlash);
	if (bonus > 0) {
		int level = (bonus + 7) >> 3;
		if (level > PAL_FLASH_GOLD_COUNT)
			level = PAL_FLASH_GOLD_COUNT;
		return PAL_FLASH_GOLD_FIRST + level - 1;
	}
	return 0;
}
