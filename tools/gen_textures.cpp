/*
 * Generates the placeholder wall/flat textures in assets/textures/ and the
 * sprites in assets/sprites/ as 64x64 8-bit PCX files using the built-in
 * palette. Sprite backgrounds are index 255 (magenta = transparent).
 * Run from the repo root:
 *
 *     ./build/gen_textures
 *
 * The output is checked in; rerun only to change the art.
 */

#include "Palette.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum { SIZE = 64 };

typedef uint8_t Image[SIZE][SIZE];	/* [y][x], row-major like the file */

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static uint32_t hash(uint32_t x, uint32_t y, uint32_t seed)
{
	uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

/* Integer noise in [-range, range]. */
static int noise(int x, int y, uint32_t seed, int range)
{
	return (int)(hash((uint32_t)x, (uint32_t)y, seed) % (uint32_t)(2 * range + 1)) - range;
}

/* Palette index for ramp/shade with the shade clamped to the ramp. */
static uint8_t col(int ramp, int shade)
{
	if (shade < 0) shade = 0;
	if (shade > 15) shade = 15;
	return (uint8_t)(ramp * 16 + shade);
}

/*
 * Staggered block wall: blocks of bw x bh with 1-pixel mortar, every other
 * row shifted by half a block. Each block gets its own base shade, the top
 * and left edges are lit and the bottom and right are shaded.
 */
static void blocks(Image img, int bw, int bh, int ramp, int base, int mortarRamp, int mortarShade, uint32_t seed)
{
	for (int y = 0; y < SIZE; y++) {
		int row = y / bh;
		int by = y % bh;
		for (int x = 0; x < SIZE; x++) {
			int sx = (x + (row & 1) * (bw / 2)) % SIZE;
			int column = sx / bw;
			int bx = sx % bw;
			if (by == bh - 1 || bx == bw - 1) {
				img[y][x] = col(mortarRamp, mortarShade + noise(x, y, seed + 1, 1));
				continue;
			}
			int shade = base + noise(column, row, seed, 1) + noise(x, y, seed + 2, 1);
			if (by == 0 || bx == 0)
				shade += 2;
			else if (by == bh - 2 || bx == bw - 2)
				shade -= 2;
			img[y][x] = col(ramp, shade);
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Textures                                                                  */
/* ------------------------------------------------------------------------- */

/* 1: gray stone blocks, the default wall '#'. */
static void tex_stone(Image img)
{
	blocks(img, 32, 16, 0, 9, 0, 3, 11);
}

/* 2: red brick. */
static void tex_brick(Image img)
{
	blocks(img, 16, 8, 1, 9, 0, 7, 22);
}

/* 3: vertical wooden planks. */
static void tex_wood(Image img)
{
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			int plank = x / 16;
			int px = x % 16;
			if (px == 15) {
				img[y][x] = col(2, 2);
				continue;
			}
			/* Grain: per-column streaks that drift slowly down the plank. */
			int grain = noise(px + (y / 9 + plank * 3) % 3, plank, 33, 2);
			int shade = 8 + grain + noise(x, y, 34, 1);
			if (px == 0)
				shade += 2;
			/* A knot in each plank. */
			int kx = px - 7, ky = y - (13 + plank * 17) % SIZE;
			if (kx * kx + ky * ky * 2 < 10)
				shade = 4;
			img[y][x] = col(2, shade);
		}
	}
}

/* 4: stone with moss growing up from the bottom. */
static void tex_mossy(Image img)
{
	blocks(img, 32, 16, 0, 9, 0, 3, 44);
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			int chance = y * 3 / 2 + noise(x / 3, y / 3, 45, 24);
			if (chance > 60)
				img[y][x] = col(6, 7 + noise(x, y, 46, 2));
		}
	}
}

/* 5: blue glazed tiles. */
static void tex_tiles(Image img)
{
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			int tx = x % 16, ty = y % 16;
			if (tx == 15 || ty == 15) {
				img[y][x] = col(0, 11);
				continue;
			}
			int shade = 10 + noise(x / 16, y / 16, 55, 1);
			if (tx == 0 || ty == 0)
				shade += 3;
			else if (tx == 14 || ty == 14)
				shade -= 2;
			/* Glaze highlight in the top-left of each tile. */
			if (tx + ty > 3 && tx + ty < 6)
				shade += 2;
			img[y][x] = col(11, shade);
		}
	}
}

/* 6: riveted metal panels. */
static void tex_metal(Image img)
{
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			int px = x % 32, py = y % 32;
			int shade = 8 + noise(x, y / 4, 66, 1);
			if (px == 0 || py == 0)
				shade = 12;
			else if (px == 31 || py == 31)
				shade = 3;
			int rx = (px < 16 ? px - 4 : px - 27), ry = (py < 16 ? py - 4 : py - 27);
			if (rx * rx + ry * ry <= 2)
				shade = (rx + ry < 0) ? 14 : 4;
			img[y][x] = col(0, shade);
		}
	}
}

/* 7: purple dungeon stone, large uneven blocks. */
static void tex_purple(Image img)
{
	blocks(img, 21, 13, 13, 8, 13, 2, 77);
}

/* 8: dark teal tech panel with lit conduits. */
static void tex_tech(Image img)
{
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			int shade = 4 + noise(x / 2, y / 2, 88, 1);
			if (x % 32 == 0 || y % 32 == 0)
				shade = 8;
			if ((x % 32 == 16 && y % 32 > 4 && y % 32 < 28) || (y % 32 == 16 && x % 32 > 4 && x % 32 < 28))
				shade = 13;
			if ((x % 32 == 16 || x % 32 == 15 || x % 32 == 17) && (y % 32 == 16 || y % 32 == 15 || y % 32 == 17))
				shade = 15;
			img[y][x] = col(9, shade);
		}
	}
}

/* 9: yellow and black hazard stripes over a metal frame. */
static void tex_hazard(Image img)
{
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			if (y < 4 || y >= 60) {
				img[y][x] = col(0, (y == 0 || y == 60) ? 11 : 6);
				continue;
			}
			bool stripe = ((x + y) / 8) % 2 == 0;
			int n = noise(x, y, 99, 1);
			img[y][x] = stripe ? col(3, 13 + n) : col(0, 2 + n);
		}
	}
}

/* Door: blue steel with a frame and a handle on the right (asymmetric on purpose). */
static void tex_door(Image img)
{
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			int shade = 8 + noise(x, y / 3, 101, 1);
			if (x < 3 || x > 60 || y < 3 || y > 60)
				shade = (x < 2 || y < 2) ? 12 : 5;
			else if (y % 15 == 7)
				shade = 5;
			else if (y % 15 == 8)
				shade = 11;
			img[y][x] = col(12, shade);

			if (x >= 50 && x <= 54 && y >= 28 && y <= 36)
				img[y][x] = col(3, (x == 50 || y == 28) ? 15 : 11);
		}
	}
}

/*
 * Exit: Wolf3D's elevator, flattened onto one wall. Two brushed steel door
 * leaves with a dark seam, a lit EXIT sign above them and a switch plate.
 */
static void tex_exit(Image img)
{
	/* 3x5 letters, drawn 2x: E X I T */
	static const uint8_t LETTERS[4][5] = {
		{ 7, 4, 6, 4, 7 }, { 5, 5, 2, 5, 5 }, { 7, 2, 2, 2, 7 }, { 7, 2, 2, 2, 2 },
	};
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			int shade = 9 + noise(x / 8, y, 171, 1);	/* vertical brushing */
			if (x < 3 || x > 60 || y < 3 || y > 60)
				shade = (x < 2 || y < 2) ? 13 : 4;	/* frame */
			else if (x == 31)
				shade = 2;				/* seam between the leaves */
			else if (x == 32)
				shade = 12;
			img[y][x] = col(0, shade);
		}
	}
	/* Sign: dark box with green letters and a lit rim. */
	for (int y = 6; y <= 20; y++) {
		for (int x = 12; x <= 51; x++)
			img[y][x] = (y == 6 || y == 20 || x == 12 || x == 51) ? col(5, 6) : col(0, 1);
	}
	for (int i = 0; i < 4; i++) {
		int left = 17 + i * 8;
		for (int r = 0; r < 5; r++) {
			for (int c = 0; c < 3; c++) {
				if (!(LETTERS[i][r] & (4 >> c)))
					continue;
				for (int py = 0; py < 2; py++) {
					for (int px = 0; px < 2; px++)
						img[9 + r * 2 + py][left + c * 2 + px] = col(5, py == 0 ? 15 : 13);
				}
			}
		}
	}
	/* Switch plate on the right leaf, lever pointing up. */
	for (int y = 34; y <= 46; y++) {
		for (int x = 44; x <= 52; x++)
			img[y][x] = (x == 44 || y == 34) ? col(0, 13) : (x == 52 || y == 46) ? col(0, 3) : col(0, 6);
	}
	for (int y = 36; y <= 40; y++)
		img[y][48] = col(1, 13);
	img[36][47] = col(1, 15);
}

/* Door frame: steel channel with grooves and bolts, seen on walls beside a door. */
static void tex_doorside(Image img)
{
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			int shade = 7 + noise(x / 2, y, 141, 1);
			if (x % 16 == 0)
				shade = 3;
			else if (x % 16 == 1)
				shade = 10;
			int bx = x % 16 - 8, by = y % 16 - 8;
			if (bx * bx + by * by <= 2)
				shade = (bx + by < 0) ? 13 : 4;
			img[y][x] = col(12, shade);
		}
	}
}

/* Floor: large worn flagstones, darker and browner than the walls. */
static void tex_floor(Image img)
{
	blocks(img, 32, 32, 0, 6, 0, 2, 111);
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			/* Scuffs and cracks. */
			if (noise(x / 2, y / 2, 112, 20) > 18)
				img[y][x] = col(0, 3);
			else if (hash((uint32_t)x, (uint32_t)y, 113) % 23 == 0)
				img[y][x] = col(2, 5);
		}
	}
}

/* Ceiling: dark wooden boards running east-west with a beam every 32 texels. */
static void tex_ceiling(Image img)
{
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			int board = y / 8;
			int shade = 5 + noise(board, x / 6, 121, 1) + noise(x, y, 122, 1);
			if (y % 8 == 7)
				shade = 2;
			if (y % 32 < 3)
				shade = (y % 32 == 0) ? 7 : 3;
			img[y][x] = col(2, shade);
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Sprites: 64x64, 1 world unit, feet on row 63. Background is transparent.  */
/* ------------------------------------------------------------------------- */

static const uint8_t CLEAR = PAL_TRANSPARENT;

static void clear(Image img)
{
	memset(img, CLEAR, sizeof(Image));
}

/* Filled rectangle, x0..x1 and y0..y1 exclusive of the end. */
static void rect(Image img, int x0, int y0, int x1, int y1, uint8_t c)
{
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			if (x >= 0 && x < SIZE && y >= 0 && y < SIZE)
				img[y][x] = c;
}

static void ellipse(Image img, int cx, int cy, int rx, int ry, uint8_t c)
{
	for (int y = cy - ry; y <= cy + ry; y++)
		for (int x = cx - rx; x <= cx + rx; x++) {
			int dx = x - cx, dy = y - cy;
			if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry
				&& x >= 0 && x < SIZE && y >= 0 && y < SIZE)
				img[y][x] = c;
		}
}

/* Darken every opaque pixel that touches the background: a cheap outline. */
static void outline(Image img)
{
	static Image src;
	memcpy(src, img, sizeof(Image));
	for (int y = 0; y < SIZE; y++)
		for (int x = 0; x < SIZE; x++) {
			if (src[y][x] == CLEAR)
				continue;
			bool edge = x == 0 || y == 0 || x == SIZE - 1 || y == SIZE - 1
				|| src[y][x - 1] == CLEAR || src[y][x + 1] == CLEAR
				|| src[y - 1][x] == CLEAR || src[y + 1][x] == CLEAR;
			if (edge)
				img[y][x] = col(src[y][x] / 16, src[y][x] % 16 - 5);
		}
}

enum Pose { POSE_STAND, POSE_WALK1, POSE_WALK2, POSE_SHOOT, POSE_PAIN };

/* Guard in an olive uniform holding a rifle, facing the camera. */
static void draw_guard(Image img, Pose pose)
{
	clear(img);
	const uint8_t uniform = col(4, 7), uniformDark = col(4, 5);
	const uint8_t skin = pose == POSE_PAIN ? col(1, 12) : col(2, 13);
	const uint8_t steel = col(0, 4), boot = col(0, 2);
	const int lean = pose == POSE_PAIN ? -2 : 0;	/* knocked back when hit */

	/* Legs: apart, together, or one lifted mid-stride. */
	int leftX = 24, rightX = 33, leftLift = 0, rightLift = 0;
	if (pose == POSE_WALK1) { leftX = 21; rightX = 36; }
	if (pose == POSE_WALK2) { leftLift = 3; rightX = 32; }
	rect(img, leftX, 43, leftX + 7, 58 - leftLift, uniformDark);
	rect(img, rightX, 43, rightX + 7, 58 - rightLift, uniformDark);
	rect(img, leftX - 1, 58 - leftLift, leftX + 7, 64 - leftLift, boot);
	rect(img, rightX, 58 - rightLift, rightX + 8, 64 - rightLift, boot);

	int x = lean;
	ellipse(img, 32 + x, 25, 13, 4, uniform);		/* shoulders */
	rect(img, 21 + x, 25, 43 + x, 42, uniform);		/* torso */
	rect(img, 17 + x, 26, 22 + x, 40, uniform);		/* arms */
	rect(img, 42 + x, 26, 47 + x, 40, uniform);
	rect(img, 17 + x, 39, 22 + x, 42, skin);		/* hands */
	rect(img, 42 + x, 33, 47 + x, 37, skin);
	rect(img, 21 + x, 41, 43 + x, 43, col(0, 3));		/* belt */
	rect(img, 30 + x, 41, 34 + x, 43, col(3, 13));		/* buckle */
	rect(img, 31 + x, 27, 33 + x, 41, uniformDark);		/* tunic seam */
	if (pose == POSE_PAIN)
		ellipse(img, 27 + x, 32, 3, 3, col(1, 8));	/* wound */
	ellipse(img, 32 + x, 17, 6, 7, skin);			/* head */
	rect(img, 29 + x, 17, 31 + x, 18, col(0, 1));		/* eyes */
	rect(img, 34 + x, 17, 36 + x, 18, col(0, 1));
	if (pose == POSE_PAIN)
		ellipse(img, 32 + x, 21, 2, 1, col(0, 1));	/* open mouth */
	else
		rect(img, 30 + x, 21, 35 + x, 22, col(2, 8));	/* mouth */
	ellipse(img, 32 + x, 12, 8, 5, col(4, 6));		/* helmet */
	rect(img, 23 + x, 14, 42 + x, 15, col(4, 4));		/* helmet rim */
	rect(img, 28 + x, 33, 56 + x, 37, steel);		/* rifle */
	rect(img, 28 + x, 36, 33 + x, 40, col(2, 5));		/* stock */
	rect(img, 54 + x, 32, 56 + x, 33, steel);		/* front sight */
	if (pose == POSE_SHOOT) {
		ellipse(img, 59, 35, 4, 4, col(3, 14));		/* muzzle flash */
		ellipse(img, 59, 35, 2, 2, col(0, 15));
		rect(img, 56, 34, 64, 36, col(3, 15));
	}
	outline(img);
}

static void spr_enemy_stand(Image img) { draw_guard(img, POSE_STAND); }
static void spr_enemy_walk1(Image img) { draw_guard(img, POSE_WALK1); }
static void spr_enemy_walk2(Image img) { draw_guard(img, POSE_WALK2); }
static void spr_enemy_shoot(Image img) { draw_guard(img, POSE_SHOOT); }
static void spr_enemy_pain(Image img)  { draw_guard(img, POSE_PAIN); }

/* Dead guard lying on his back in a pool of blood, helmet rolled away. */
static void spr_enemy_dead(Image img)
{
	clear(img);
	ellipse(img, 32, 61, 24, 2, col(1, 5));			/* blood */
	rect(img, 16, 54, 46, 61, col(4, 6));			/* body */
	rect(img, 46, 55, 58, 60, col(4, 5));			/* legs */
	rect(img, 57, 54, 62, 61, col(0, 2));			/* boots */
	ellipse(img, 12, 57, 4, 4, col(2, 12));			/* head */
	rect(img, 18, 61, 40, 63, col(0, 4));			/* rifle on the floor */
	ellipse(img, 4, 61, 4, 2, col(4, 6));			/* helmet */
	outline(img);
}

/* First-person pistol seen from behind, held in a gloved hand with a blue sleeve. */
static void draw_pistol(Image img, bool firing)
{
	clear(img);
	rect(img, 20, 54, 44, 64, col(11, 6));			/* sleeve */
	rect(img, 20, 54, 44, 55, col(11, 9));
	ellipse(img, 32, 47, 10, 9, col(2, 11));		/* hand */
	ellipse(img, 26, 46, 3, 3, col(2, 9));			/* thumb */
	rect(img, 27, 30, 38, 46, col(0, 3));			/* grip */
	rect(img, 26, 18, 39, 32, col(0, 6));			/* slide */
	rect(img, 26, 18, 39, 20, col(0, 10));			/* top highlight */
	rect(img, 31, 15, 34, 18, col(0, 5));			/* rear sight */
	ellipse(img, 32, 22, 2, 1, col(0, 1));			/* barrel bore */
	if (firing) {
		ellipse(img, 32, 9, 11, 8, col(3, 13));		/* muzzle flash */
		ellipse(img, 32, 9, 6, 5, col(3, 15));
		ellipse(img, 32, 10, 3, 2, col(0, 15));
		rect(img, 31, 0, 34, 16, col(3, 14));		/* spikes */
		rect(img, 20, 8, 44, 10, col(3, 14));
	}
	outline(img);
}

static void spr_weapon_idle(Image img) { draw_pistol(img, false); }
static void spr_weapon_fire(Image img) { draw_pistol(img, true); }

/* A clenched fist seen from behind: knuckles on top, fingers folded, thumb across. */
static void draw_fist(Image img, int cx, int cy, int r)
{
	ellipse(img, cx, cy, r, r * 4 / 5, col(2, 11));
	for (int f = 0; f < 4; f++) {			/* knuckles */
		int kx = cx - r + r / 4 + f * r / 2;
		ellipse(img, kx, cy - r * 3 / 5, r / 4 + 1, r / 4, col(2, 13));
	}
	for (int f = 1; f < 4; f++) {			/* gaps between the fingers */
		int gx = cx - r + f * r / 2;
		rect(img, gx, cy - r / 3, gx + 1, cy + r / 3, col(2, 7));
	}
	rect(img, cx - r + 2, cy + r / 3, cx + r / 3, cy + r / 3 + 2, col(2, 9));	/* thumb */
}

static void draw_fists(Image img, bool punching)
{
	clear(img);
	if (punching) {
		/* Right arm thrown forward to the middle: the sleeve tapers away from the bottom right. */
		for (int y = 34; y < 64; y++) {
			float f = (float)(y - 34) / 30.0f;
			int cx = 36 + (int)(f * 14.0f), half = 5 + (int)(f * 5.0f);
			rect(img, cx - half, y, cx + half, y + 1, col(11, 5 + (int)(f * 3.0f)));
		}
		draw_fist(img, 35, 30, 11);
		draw_fist(img, 10, 60, 9);			/* left fist held back, low */
		rect(img, 2, 62, 18, 64, col(11, 6));
	} else {
		rect(img, 4, 56, 20, 64, col(11, 6));		/* sleeves */
		rect(img, 44, 56, 60, 64, col(11, 6));
		draw_fist(img, 12, 50, 9);
		draw_fist(img, 52, 50, 9);
	}
	outline(img);
}

static void spr_fists_idle(Image img)  { draw_fists(img, false); }
static void spr_fists_punch(Image img) { draw_fists(img, true); }

/* First-person submachine gun: perforated barrel shroud up the middle, magazine down the left. */
static void draw_smg(Image img, bool firing)
{
	clear(img);
	rect(img, 18, 56, 30, 64, col(11, 6));			/* left sleeve */
	rect(img, 36, 56, 50, 64, col(11, 6));			/* right sleeve */
	rect(img, 21, 30, 27, 56, col(0, 2));			/* magazine */
	rect(img, 21, 30, 22, 56, col(0, 5));
	rect(img, 27, 22, 38, 52, col(0, 5));			/* receiver */
	rect(img, 27, 22, 38, 24, col(0, 9));			/* top highlight */
	rect(img, 29, 12, 36, 22, col(0, 3));			/* barrel shroud */
	for (int y = 14; y < 21; y += 3)
		rect(img, 31, y, 34, y + 1, col(0, 1));	/* cooling holes */
	rect(img, 31, 9, 34, 12, col(0, 6));			/* front sight */
	rect(img, 30, 20, 35, 22, col(0, 7));			/* rear sight */
	ellipse(img, 24, 52, 7, 6, col(2, 11));			/* left hand on the magazine well */
	ellipse(img, 41, 50, 8, 7, col(2, 11));			/* right hand on the grip */
	ellipse(img, 36, 47, 3, 3, col(2, 9));			/* right thumb */
	if (firing) {
		ellipse(img, 32, 5, 10, 5, col(3, 13));		/* muzzle flash */
		ellipse(img, 32, 5, 5, 3, col(3, 15));
		rect(img, 19, 4, 46, 6, col(3, 14));
		rect(img, 31, 0, 34, 9, col(3, 14));
		ellipse(img, 32, 5, 2, 1, col(0, 15));
	}
	outline(img);
}

static void spr_smg_idle(Image img) { draw_smg(img, false); }
static void spr_smg_fire(Image img) { draw_smg(img, true); }

/* First aid kit lying on the floor. */
static void spr_health(Image img)
{
	clear(img);
	rect(img, 19, 46, 45, 64, col(0, 14));
	rect(img, 19, 46, 45, 48, col(0, 15));		/* lid highlight */
	rect(img, 29, 49, 35, 62, col(1, 12));		/* cross */
	rect(img, 24, 52, 40, 58, col(1, 12));
	rect(img, 28, 43, 36, 46, col(0, 9));		/* handle */
	outline(img);
}

/* Box of rifle rounds with a few cartridges on top. */
static void spr_ammo(Image img)
{
	clear(img);
	rect(img, 21, 51, 43, 64, col(2, 6));
	rect(img, 21, 51, 43, 53, col(2, 9));
	rect(img, 25, 56, 39, 60, col(3, 12));		/* label */
	for (int i = 0; i < 5; i++) {
		int x = 23 + i * 4;
		rect(img, x, 43, x + 3, 51, col(3, 12));	/* brass */
		rect(img, x, 41, x + 3, 43, col(2, 7));		/* bullet tip */
	}
	outline(img);
}

/* Submachine gun lying on its side on the floor: muzzle left, folded stock right. */
static void spr_smg_pickup(Image img)
{
	clear(img);
	rect(img, 6, 52, 18, 55, col(0, 3));			/* barrel shroud */
	for (int x = 8; x < 17; x += 3)
		rect(img, x, 53, x + 1, 54, col(0, 1));
	rect(img, 3, 53, 6, 54, col(0, 6));			/* muzzle */
	rect(img, 18, 50, 42, 56, col(0, 5));			/* receiver */
	rect(img, 18, 50, 42, 51, col(0, 9));
	rect(img, 24, 56, 28, 64, col(0, 2));			/* magazine */
	rect(img, 35, 56, 39, 62, col(2, 5));			/* grip */
	rect(img, 42, 52, 58, 54, col(0, 6));			/* folded stock */
	rect(img, 56, 52, 59, 59, col(0, 6));
	outline(img);
}

/* Rusty green oil drum, shaded as a cylinder. */
static void spr_barrel(Image img)
{
	clear(img);
	for (int y = 24; y < 64; y++)
		for (int x = 18; x < 46; x++) {
			int fromCentre = x - 29;		/* light from the upper left */
			int shade = 10 - (fromCentre * fromCentre) / 40 + noise(x, y, 131, 1);
			bool band = (y >= 30 && y < 32) || (y >= 46 && y < 48) || y >= 61;
			img[y][x] = band ? col(0, shade - 2) : col(6, shade - 2);
			if (hash((uint32_t)x, (uint32_t)y, 132) % 17 == 0)
				img[y][x] = col(2, 5);		/* rust */
		}
	ellipse(img, 32, 24, 14, 3, col(6, 4));		/* lid */
	ellipse(img, 26, 24, 3, 1, col(6, 2));		/* bung */
	outline(img);
}

/*
 * Ragged ball of fire centred at (cx, cy): white-hot core, yellow, orange,
 * red rim. heat < 1 cools it down (frame 3: dull fire and gray smoke).
 */
static void fireball(Image img, int cx, int cy, int r, float heat, uint32_t seed)
{
	for (int y = cy - r - 3; y <= cy + r + 3; y++)
		for (int x = cx - r - 3; x <= cx + r + 3; x++) {
			if (x < 0 || x >= SIZE || y < 0 || y >= SIZE)
				continue;
			int dx = x - cx, dy = y - cy;
			float d = sqrtf((float)(dx * dx + dy * dy)) + (float)noise(x / 2, y / 2, seed, 3);
			float t = d / (float)r;		/* 0 centre, 1 edge */
			if (t > 1.0f)
				continue;
			float hot = (1.0f - t) * heat + (float)noise(x, y, seed + 1, 1) * 0.08f;
			uint8_t c;
			if (hot > 0.75f)      c = col(3, 15);		/* white-yellow */
			else if (hot > 0.5f)  c = col(3, 12);		/* yellow */
			else if (hot > 0.3f)  c = col(2, 12);		/* orange */
			else if (hot > 0.15f) c = col(1, 10);		/* red */
			else                  c = col(0, 4 + noise(x, y, seed + 2, 2));	/* smoke */
			img[y][x] = c;
		}
}

/* Barrel bursting: the drum with fire breaking out of the lid. */
static void spr_explode1(Image img)
{
	spr_barrel(img);
	fireball(img, 32, 22, 11, 0.9f, 140);
	fireball(img, 25, 30, 5, 0.8f, 141);
	fireball(img, 40, 33, 5, 0.8f, 142);
}

/* The blast: a big fireball where the barrel stood. */
static void spr_explode2(Image img)
{
	clear(img);
	fireball(img, 32, 38, 25, 1.1f, 150);
}

/* Burning out: smaller, cooler, smoky. */
static void spr_explode3(Image img)
{
	clear(img);
	fireball(img, 32, 34, 22, 0.55f, 160);
	fireball(img, 30, 48, 10, 0.8f, 161);
}

/* Lamp hanging from the ceiling on a chain. */
static void spr_lamp(Image img)
{
	clear(img);
	rect(img, 31, 0, 33, 13, col(0, 6));			/* chain */
	for (int y = 13; y < 24; y++) {
		int half = 3 + (y - 13);			/* widening shade */
		rect(img, 32 - half, y, 32 + half, y + 1, col(3, 9 + (24 - y) / 4));
	}
	rect(img, 18, 23, 46, 25, col(3, 6));			/* rim */
	ellipse(img, 32, 26, 5, 3, col(3, 15));			/* bulb */
	outline(img);
}

/* ------------------------------------------------------------------------- */
/* PCX writer                                                                */
/* ------------------------------------------------------------------------- */

static void put_u16(uint8_t *p, int v)
{
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)(v >> 8);
}

static bool write_pcx(const char *path, Image img, const Palette *pal)
{
	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "Cannot write %s\n", path);
		return false;
	}

	uint8_t header[128];
	memset(header, 0, sizeof(header));
	header[0] = 0x0A;		/* ZSoft */
	header[1] = 5;			/* version 3.0+, 256-colour palette */
	header[2] = 1;			/* RLE */
	header[3] = 8;			/* bits per pixel per plane */
	put_u16(header + 8, SIZE - 1);	/* xmax */
	put_u16(header + 10, SIZE - 1);	/* ymax */
	put_u16(header + 12, 72);	/* dpi */
	put_u16(header + 14, 72);
	header[65] = 1;			/* planes */
	put_u16(header + 66, SIZE);	/* bytes per line (even) */
	put_u16(header + 68, 1);	/* colour palette */
	fwrite(header, 1, sizeof(header), f);

	/* RLE per scanline: runs of up to 63, and any byte >= 0xC0 must be escaped as a run. */
	for (int y = 0; y < SIZE; y++) {
		int x = 0;
		while (x < SIZE) {
			uint8_t v = img[y][x];
			int run = 1;
			while (x + run < SIZE && run < 63 && img[y][x + run] == v)
				run++;
			if (run > 1 || v >= 0xC0)
				fputc(0xC0 | run, f);
			fputc(v, f);
			x += run;
		}
	}

	fputc(0x0C, f);
	fwrite(pal->rgb, 1, sizeof(pal->rgb), f);
	fclose(f);
	return true;
}

int main()
{
	Palette pal;
	palette_build_default(&pal);

	struct Entry { const char *path; void (*make)(Image); };
	const Entry entries[] = {
		{ "assets/textures/wall1.pcx", tex_stone },
		{ "assets/textures/wall2.pcx", tex_brick },
		{ "assets/textures/wall3.pcx", tex_wood },
		{ "assets/textures/wall4.pcx", tex_mossy },
		{ "assets/textures/wall5.pcx", tex_tiles },
		{ "assets/textures/wall6.pcx", tex_metal },
		{ "assets/textures/wall7.pcx", tex_purple },
		{ "assets/textures/wall8.pcx", tex_tech },
		{ "assets/textures/wall9.pcx", tex_hazard },
		{ "assets/textures/door.pcx", tex_door },
		{ "assets/textures/doorside.pcx", tex_doorside },
		{ "assets/textures/exit.pcx", tex_exit },
		{ "assets/textures/floor.pcx", tex_floor },
		{ "assets/textures/ceiling.pcx", tex_ceiling },
		{ "assets/sprites/enemy_stand.pcx", spr_enemy_stand },
		{ "assets/sprites/enemy_walk1.pcx", spr_enemy_walk1 },
		{ "assets/sprites/enemy_walk2.pcx", spr_enemy_walk2 },
		{ "assets/sprites/enemy_shoot.pcx", spr_enemy_shoot },
		{ "assets/sprites/enemy_pain.pcx", spr_enemy_pain },
		{ "assets/sprites/enemy_dead.pcx", spr_enemy_dead },
		{ "assets/sprites/weapon_idle.pcx", spr_weapon_idle },
		{ "assets/sprites/weapon_fire.pcx", spr_weapon_fire },
		{ "assets/sprites/health.pcx", spr_health },
		{ "assets/sprites/ammo.pcx", spr_ammo },
		{ "assets/sprites/barrel.pcx", spr_barrel },
		{ "assets/sprites/lamp.pcx", spr_lamp },
		{ "assets/sprites/explode1.pcx", spr_explode1 },
		{ "assets/sprites/explode2.pcx", spr_explode2 },
		{ "assets/sprites/explode3.pcx", spr_explode3 },
		{ "assets/sprites/fists_idle.pcx", spr_fists_idle },
		{ "assets/sprites/fists_punch.pcx", spr_fists_punch },
		{ "assets/sprites/smg_idle.pcx", spr_smg_idle },
		{ "assets/sprites/smg_fire.pcx", spr_smg_fire },
		{ "assets/sprites/smg_pickup.pcx", spr_smg_pickup },
	};

	for (const Entry &e : entries) {
		static Image img;
		e.make(img);
		if (!write_pcx(e.path, img, &pal))
			return 1;
		printf("wrote %s\n", e.path);
	}
	return 0;
}
