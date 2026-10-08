#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>

#include "Synth.h"

/*
 * Standard MIDI File (.mid) loading and playback through Synth.
 *
 * midi_load parses format 0 and 1 files (running status, tempo changes,
 * sysex and other meta events skipped), merges all tracks and converts
 * every event's tick to an absolute sample position at the given rate.
 */

struct MidiEvent {
	uint32_t sample;	/* when it happens, in output samples from the song start */
	uint8_t status;		/* channel message status, 0x80-0xEF */
	uint8_t data1;
	uint8_t data2;
};

struct Song {
	MidiEvent *events;
	int count;
	uint32_t length;	/* samples; the end-of-track of the longest track */
};

bool midi_load(Song *song, const char *path, int sampleRate);
void midi_free(Song *song);

/* Plays a Song through a Synth, looping at the end. */
struct MusicPlayer {
	Song song;
	Synth synth;
	uint32_t position;	/* samples into the song */
	int next;		/* next event to send */
};

/* soundfont: .sf2 path, or null / "" to search for one (see synth_init). */
bool music_open(MusicPlayer *player, const char *path, int sampleRate, const char *soundfont);
void music_close(MusicPlayer *player);

/* Render `frames` stereo frames, sending events at their exact sample. */
void music_render(MusicPlayer *player, int16_t *out, int frames);

#endif
