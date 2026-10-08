/*
 * Renders a MIDI file through the engine's synth into a 44.1 kHz stereo
 * WAV, to listen to or inspect music without running the game:
 *
 *     ./build/render_midi assets/music/e1m1.mid out.wav [seconds]
 *
 * Without `seconds` it renders exactly one pass of the song.
 */

#include "Midi.h"

#include <stdio.h>
#include <stdlib.h>

enum { RATE = 44100, CHUNK = 4096 };

static void put_u32(FILE *f, uint32_t v)
{
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
	fwrite(b, 1, 4, f);
}

static void put_u16(FILE *f, uint16_t v)
{
	uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
	fwrite(b, 1, 2, f);
}

int main(int argc, char **argv)
{
	if (argc != 3 && argc != 4) {
		fprintf(stderr, "usage: %s <in.mid> <out.wav> [seconds]\n", argv[0]);
		return 1;
	}

	static MusicPlayer player;
	if (!music_open(&player, argv[1], RATE))
		return 1;
	uint32_t frames = argc == 4 ? (uint32_t)(atof(argv[3]) * RATE) : player.song.length;
	printf("%s: %d events, %.2f s per loop\n", argv[1], player.song.count,
		(double)player.song.length / RATE);

	FILE *f = fopen(argv[2], "wb");
	if (!f) {
		fprintf(stderr, "Cannot write %s\n", argv[2]);
		return 1;
	}
	uint32_t dataBytes = frames * 4;
	fwrite("RIFF", 1, 4, f);
	put_u32(f, 36 + dataBytes);
	fwrite("WAVEfmt ", 1, 8, f);
	put_u32(f, 16);
	put_u16(f, 1);			/* PCM */
	put_u16(f, 2);			/* stereo */
	put_u32(f, RATE);
	put_u32(f, RATE * 4);
	put_u16(f, 4);
	put_u16(f, 16);
	fwrite("data", 1, 4, f);
	put_u32(f, dataBytes);

	static int16_t buf[CHUNK * 2];
	for (uint32_t done = 0; done < frames; ) {
		int n = (int)(frames - done < CHUNK ? frames - done : CHUNK);
		music_render(&player, buf, n);
		fwrite(buf, sizeof(int16_t), (size_t)n * 2, f);	/* x86-64 is little-endian like WAV */
		done += (uint32_t)n;
	}
	fclose(f);
	music_close(&player);
	printf("wrote %s (%.2f s)\n", argv[2], (double)frames / RATE);
	return 0;
}
