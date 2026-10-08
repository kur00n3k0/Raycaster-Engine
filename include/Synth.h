#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

/*
 * MIDI synthesizer: a thin wrapper around FluidSynth playing a General MIDI
 * SoundFont (.sf2). Channel 10 is the drum kit, as in GM.
 *
 * Takes raw MIDI channel messages and renders interleaved stereo 16-bit
 * PCM. No audio driver and no OpenAL in here: FluidSynth only renders on
 * request, so it is usable offline and the caller decides where samples go.
 */

struct _fluid_hashtable_t;
struct _fluid_synth_t;

struct Synth {
	_fluid_hashtable_t *settings;	/* fluid_settings_t */
	_fluid_synth_t *fluid;		/* fluid_synth_t */
};

/*
 * soundfont: path to a .sf2, or null / "" to search the usual places
 * (assets/music, FluidSynth's compiled-in default, /usr/share/soundfonts,
 * /usr/share/sounds/sf2). Returns false if none can be loaded.
 */
bool synth_init(Synth *synth, int sampleRate, const char *soundfont);
void synth_shutdown(Synth *synth);

/* One MIDI channel message: status byte (0x80-0xEF) and its data bytes. */
void synth_message(Synth *synth, uint8_t status, uint8_t data1, uint8_t data2);

/* Release every sounding note (used when a song loops or stops). */
void synth_all_notes_off(Synth *synth);

/* Render `frames` stereo frames into out[frames * 2]. */
void synth_render(Synth *synth, int16_t *out, int frames);

#endif
