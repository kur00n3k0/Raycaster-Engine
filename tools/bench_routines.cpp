/*
 * Times every ASM inner loop against its C++ reference on frame-sized
 * workloads and prints nanoseconds per pixel and the speedup. Build it in a
 * Release tree for numbers that mean anything:
 *
 *     cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
 *     cmake --build build-release -j && ./build-release/bench_routines
 */

#include "AsmRoutines.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

enum { W = 320, H = 200, TEX = 64, MIN_SECONDS_X10 = 3 };

static uint8_t g_fb[W * H];
static uint8_t g_tex[TEX * TEX];
static uint8_t g_sprite[TEX * TEX];
static uint8_t g_cmap[256];

static double now_seconds()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* One full 320x200 frame of each workload. */
static void frame_clear(bool asmPath)
{
	if (asmPath)
		rc_fb_clear(g_fb, W * H, 7);
	else
		rc_fb_clear_ref(g_fb, W * H, 7);
}

/* Wall columns from 0.5 to 2 times magnified, like walking down a corridor. */
static void frame_columns(bool asmPath)
{
	ColumnArgs a;
	a.colormap = g_cmap;
	a.pitch = W;
	a.count = H;
	a.mask = TEX - 1;
	a.pad = 0;
	for (int x = 0; x < W; x++) {
		a.dest = g_fb + x;
		a.source = g_tex + (x & (TEX - 1)) * TEX;
		a.frac = (uint32_t)x << 12;
		a.step = 0x8000u + ((uint32_t)x << 9);
		if (asmPath)
			rc_draw_column(&a);
		else
			rc_draw_column_ref(&a);
	}
}

/* Floor rows: one span per row, both texture coordinates stepping. */
static void frame_spans(bool asmPath)
{
	SpanArgs a;
	a.source = g_tex;
	a.colormap = g_cmap;
	a.count = W;
	a.umask = TEX - 1;
	a.vmask = TEX - 1;
	a.vshift = 6;
	for (int y = 0; y < H; y++) {
		a.dest = g_fb + y * W;
		a.ufrac = (uint32_t)y << 14;
		a.vfrac = (uint32_t)y << 13;
		a.ustep = 0x4000u + ((uint32_t)y << 8);
		a.vstep = (uint32_t)-(int32_t)(0x2000u + ((uint32_t)y << 7));
		if (asmPath)
			rc_draw_span(&a);
		else
			rc_draw_span_ref(&a);
	}
}

/* Full-height masked sprite columns over the whole screen. */
static void frame_sprites(bool asmPath)
{
	ColumnArgs a;
	a.colormap = g_cmap;
	a.pitch = W;
	a.count = H;
	a.mask = TEX - 1;
	a.pad = 0;
	a.step = 0x5200u;
	for (int x = 0; x < W; x++) {
		a.dest = g_fb + x;
		a.source = g_sprite + (x & (TEX - 1)) * TEX;
		a.frac = 0;
		if (asmPath)
			rc_draw_sprite_col(&a);
		else
			rc_draw_sprite_col_ref(&a);
	}
}

/* Run `frame` until enough time has passed; nanoseconds per pixel. */
static double time_it(void (*frame)(bool), bool asmPath)
{
	frame(asmPath);	/* warm caches */
	long frames = 0;
	double start = now_seconds(), elapsed = 0.0;
	while (elapsed < MIN_SECONDS_X10 / 10.0) {
		for (int i = 0; i < 50; i++)
			frame(asmPath);
		frames += 50;
		elapsed = now_seconds() - start;
	}
	return elapsed * 1e9 / ((double)frames * W * H);
}

int main()
{
	uint32_t seed = 12345;
	for (int i = 0; i < TEX * TEX; i++) {
		seed = seed * 1664525u + 1013904223u;
		g_tex[i] = (uint8_t)(seed >> 24);
		g_sprite[i] = (seed >> 8) % 10 < 3 ? 0xFF : (uint8_t)((seed >> 16) % 255);	/* ~30% transparent */
	}
	for (int i = 0; i < 256; i++)
		g_cmap[i] = (uint8_t)(255 - i);

	struct Bench { const char *name; void (*frame)(bool); };
	const Bench benches[] = {
		{ "fb_clear", frame_clear },
		{ "draw_column", frame_columns },
		{ "draw_span", frame_spans },
		{ "draw_sprite_col", frame_sprites },
	};

	printf("%-16s %12s %12s %9s   (ns per pixel, 320x200 frames)\n", "routine", "C++ ref", "ASM", "speedup");
	for (const Bench &b : benches) {
		double ref = time_it(b.frame, false);
		double asmNs = time_it(b.frame, true);
		printf("%-16s %12.3f %12.3f %8.2fx\n", b.name, ref, asmNs, ref / asmNs);
	}

	/* Keep the compiler from discarding the work. */
	uint32_t sum = 0;
	for (int i = 0; i < W * H; i++)
		sum += g_fb[i];
	printf("checksum %u\n", sum);
	return 0;
}
