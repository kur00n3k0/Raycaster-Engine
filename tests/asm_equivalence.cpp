/*
 * Runs every ASM routine and its C++ reference on the same input and
 * compares the output byte for byte. Exit code 0 means all equal.
 */

#include "AsmRoutines.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
		fprintf(stderr, __VA_ARGS__); \
		fputc('\n', stderr); \
		g_failures++; \
	} \
} while (0)

/* Small xorshift so the test is deterministic without <random>. */
static uint32_t g_seed = 0x12345678u;
static uint32_t rnd()
{
	g_seed ^= g_seed << 13;
	g_seed ^= g_seed >> 17;
	g_seed ^= g_seed << 5;
	return g_seed;
}

/* Guard bytes around the buffer catch writes past either end. */
enum { GUARD = 64, FB_MAX = 640 * 400 };
static uint8_t g_a[GUARD + FB_MAX + GUARD];
static uint8_t g_b[GUARD + FB_MAX + GUARD];

static void fill_noise()
{
	for (size_t i = 0; i < sizeof(g_a); i++)
		g_a[i] = g_b[i] = (uint8_t)rnd();
}

static void test_fb_clear()
{
	const uint32_t counts[] = { 0, 1, 15, 16, 17, 320 * 200, 640 * 400 };
	for (uint32_t count : counts) {
		for (int color = 0; color < 256; color += 51) {
			fill_noise();
			rc_fb_clear(g_a + GUARD, count, (uint8_t)color);
			rc_fb_clear_ref(g_b + GUARD, count, (uint8_t)color);
			CHECK(memcmp(g_a, g_b, sizeof(g_a)) == 0,
				"fb_clear count=%u color=%d", count, color);
		}
	}

	/* Unaligned starts and random lengths. */
	for (int i = 0; i < 1000; i++) {
		uint32_t offset = rnd() % 16;
		uint32_t count = rnd() % (FB_MAX - 16);
		uint8_t color = (uint8_t)rnd();
		fill_noise();
		rc_fb_clear(g_a + GUARD + offset, count, color);
		rc_fb_clear_ref(g_b + GUARD + offset, count, color);
		CHECK(memcmp(g_a, g_b, sizeof(g_a)) == 0,
			"fb_clear offset=%u count=%u color=%u", offset, count, color);
	}
}

static void run_draw_column(void (*fn)(const ColumnArgs *), uint8_t *buf,
	const uint8_t *tex, const uint8_t *cmap, int x, int y, ColumnArgs args)
{
	args.dest = buf + GUARD + y * args.pitch + x;
	args.source = tex;
	args.colormap = cmap;
	fn(&args);
}

static void test_draw_column()
{
	enum { W = 320, H = 200, TEX_MAX = 128 };
	static uint8_t tex[TEX_MAX];
	static uint8_t cmap[256];
	for (int i = 0; i < TEX_MAX; i++)
		tex[i] = (uint8_t)rnd();
	for (int i = 0; i < 256; i++)
		cmap[i] = (uint8_t)rnd();

	/* Edge cases: empty, negative, single pixel, full height, wrap, zero step. */
	struct Case { int count; uint32_t frac, step, mask; };
	const Case cases[] = {
		{ 0,   0,           0x10000, 63 },
		{ -5,  0,           0x10000, 63 },
		{ 1,   0x3F0000,    0x10000, 63 },
		{ H,   0,           0x5200,  63 },	/* 64 texels over 200 rows */
		{ H,   0,           0x80000, 63 },	/* minified, wraps 25 times */
		{ H,   0xFFFF8000u, 0x9000,  63 },	/* frac wraps past 2^32 */
		{ H,   0x123456,    0,       127 },	/* zero step: one texel */
		{ H,   0,           0xFFFFFFFFu, 127 },	/* step of almost -1 */
	};
	for (const Case &c : cases) {
		ColumnArgs args = {};
		args.pitch = W;
		args.count = c.count;
		args.frac = c.frac;
		args.step = c.step;
		args.mask = c.mask;
		fill_noise();
		run_draw_column(rc_draw_column, g_a, tex, cmap, 17, 0, args);
		run_draw_column(rc_draw_column_ref, g_b, tex, cmap, 17, 0, args);
		CHECK(memcmp(g_a, g_b, sizeof(g_a)) == 0,
			"draw_column count=%d frac=%08x step=%08x mask=%u",
			c.count, c.frac, c.step, c.mask);
	}

	/* Random columns at random places, always inside the framebuffer. */
	for (int i = 0; i < 5000; i++) {
		ColumnArgs args = {};
		args.pitch = W;
		int x = (int)(rnd() % W);
		int y = (int)(rnd() % H);
		args.count = (int)(rnd() % (uint32_t)(H - y + 1));
		args.frac = rnd();
		args.step = rnd() >> (rnd() % 24);
		args.mask = (rnd() & 1) ? 63 : 127;
		fill_noise();
		run_draw_column(rc_draw_column, g_a, tex, cmap, x, y, args);
		run_draw_column(rc_draw_column_ref, g_b, tex, cmap, x, y, args);
		CHECK(memcmp(g_a, g_b, sizeof(g_a)) == 0,
			"draw_column x=%d y=%d count=%d frac=%08x step=%08x mask=%u",
			x, y, args.count, args.frac, args.step, args.mask);
	}
}

static void run_draw_span(void (*fn)(const SpanArgs *), uint8_t *buf,
	const uint8_t *tex, const uint8_t *cmap, int offset, SpanArgs args)
{
	args.dest = buf + GUARD + offset;
	args.source = tex;
	args.colormap = cmap;
	fn(&args);
}

static void test_draw_span()
{
	enum { W = 320, TEX_MAX = 128 * 128 };
	static uint8_t tex[TEX_MAX];
	static uint8_t cmap[256];
	for (int i = 0; i < TEX_MAX; i++)
		tex[i] = (uint8_t)rnd();
	for (int i = 0; i < 256; i++)
		cmap[i] = (uint8_t)rnd();

	/* Edge cases: empty, negative, single pixel, wraps, negative steps, non-square. */
	struct Case { int count; uint32_t uf, vf, us, vs; int ulog, vlog; };
	const Case cases[] = {
		{ 0,   0, 0, 0x10000, 0, 6, 6 },
		{ -3,  0, 0, 0x10000, 0, 6, 6 },
		{ 1,   0x3F0000, 0x3F0000, 0, 0, 6, 6 },
		{ W,   0, 0, 0x10000, 0, 6, 6 },
		{ W,   0xFFFF0000u, 0x8000, 0x30000, 0x1234, 6, 6 },
		{ W,   0x200000, 0x200000, (uint32_t)-0x8000, (uint32_t)-0x22000, 6, 6 },
		{ W,   0, 0, 0x7000, 0x9000, 7, 5 },
		{ W,   0x12345678, 0x9ABCDEF0, 0xFFFFFFFFu, 1, 7, 7 },
	};
	for (const Case &c : cases) {
		SpanArgs args = {};
		args.count = c.count;
		args.ufrac = c.uf;
		args.vfrac = c.vf;
		args.ustep = c.us;
		args.vstep = c.vs;
		args.umask = (1u << c.ulog) - 1;
		args.vmask = (1u << c.vlog) - 1;
		args.vshift = (uint32_t)c.vlog;
		fill_noise();
		run_draw_span(rc_draw_span, g_a, tex, cmap, 3, args);
		run_draw_span(rc_draw_span_ref, g_b, tex, cmap, 3, args);
		CHECK(memcmp(g_a, g_b, sizeof(g_a)) == 0,
			"draw_span count=%d uf=%08x vf=%08x us=%08x vs=%08x", c.count, c.uf, c.vf, c.us, c.vs);
	}

	for (int i = 0; i < 5000; i++) {
		SpanArgs args = {};
		int ulog = 3 + (int)(rnd() % 5);	/* 8 .. 128 */
		int vlog = 3 + (int)(rnd() % 5);
		int offset = (int)(rnd() % (FB_MAX - W));
		args.count = (int)(rnd() % (W + 1));
		args.ufrac = rnd();
		args.vfrac = rnd();
		args.ustep = rnd() >> (rnd() % 24);
		args.vstep = rnd() >> (rnd() % 24);
		if (rnd() & 1) args.ustep = (uint32_t)-(int32_t)args.ustep;
		if (rnd() & 1) args.vstep = (uint32_t)-(int32_t)args.vstep;
		args.umask = (1u << ulog) - 1;
		args.vmask = (1u << vlog) - 1;
		args.vshift = (uint32_t)vlog;
		fill_noise();
		run_draw_span(rc_draw_span, g_a, tex, cmap, offset, args);
		run_draw_span(rc_draw_span_ref, g_b, tex, cmap, offset, args);
		CHECK(memcmp(g_a, g_b, sizeof(g_a)) == 0,
			"draw_span offset=%d count=%d uf=%08x vf=%08x us=%08x vs=%08x u=%d v=%d",
			offset, args.count, args.ufrac, args.vfrac, args.ustep, args.vstep, ulog, vlog);
	}
}

static void test_draw_sprite_col()
{
	enum { W = 320, H = 200, TEX_MAX = 128 };
	static uint8_t tex[TEX_MAX];
	static uint8_t cmap[256];
	for (int i = 0; i < 256; i++)
		cmap[i] = (uint8_t)rnd();

	for (int i = 0; i < 5000; i++) {
		/* Vary how much of the texture is transparent: none, some, most, all. */
		uint32_t holes = rnd() % 4;
		for (int t = 0; t < TEX_MAX; t++) {
			bool hole = holes == 3 || (holes > 0 && rnd() % 3 < holes);
			tex[t] = hole ? 0xFF : (uint8_t)(rnd() % 255);
		}

		ColumnArgs args = {};
		args.pitch = W;
		int x = (int)(rnd() % W);
		int y = (int)(rnd() % H);
		args.count = (i == 0) ? 0 : (i == 1) ? -7 : (int)(rnd() % (uint32_t)(H - y + 1));
		args.frac = rnd();
		args.step = rnd() >> (rnd() % 24);
		args.mask = (rnd() & 1) ? 63 : 127;
		fill_noise();
		run_draw_column(rc_draw_sprite_col, g_a, tex, cmap, x, y, args);
		run_draw_column(rc_draw_sprite_col_ref, g_b, tex, cmap, x, y, args);
		CHECK(memcmp(g_a, g_b, sizeof(g_a)) == 0,
			"draw_sprite_col x=%d y=%d count=%d frac=%08x step=%08x mask=%u holes=%u",
			x, y, args.count, args.frac, args.step, args.mask, holes);
	}
}

int main()
{
	test_fb_clear();
	test_draw_column();
	test_draw_span();
	test_draw_sprite_col();

	if (g_failures) {
		fprintf(stderr, "%d failure(s)\n", g_failures);
		return 1;
	}
	printf("asm_equivalence: all routines match\n");
	return 0;
}
