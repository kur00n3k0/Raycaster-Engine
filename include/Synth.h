#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

/*
 * Software MIDI synthesizer: 2-operator FM voices in the spirit of the
 * OPL2/AdLib chip Doom and Wolf3D used for music. Instruments are chosen
 * by General MIDI program family; channel 10 is a synthesized drum kit.
 *
 * Takes raw MIDI channel messages and renders interleaved stereo 16-bit
 * PCM. No OpenAL in here: it is plain sample math, usable offline.
 */

enum {
	SYNTH_VOICES = 32,
	SYNTH_CHANNELS = 16,
	SYNTH_DRUM_CHANNEL = 9	/* MIDI channel 10 */
};

struct Patch;

struct Voice {
	bool active;
	bool released;		/* note-off received (or pedal lifted) */
	bool held;		/* note-off received while the sustain pedal was down */
	uint8_t channel;
	uint8_t note;
	const Patch *patch;
	float baseHz;
	float gain;		/* velocity curve */
	float carPhase;		/* 0..1 */
	float modPhase;
	float env;		/* current envelope level */
	int stage;		/* attack, decay, sustain, release */
	float attackStep;	/* envelope increase per sample during attack */
	float decayMul;		/* per-sample multipliers, precomputed at note-on */
	float releaseMul;
	float sweep;		/* current extra pitch factor (drums), decays by sweepMul */
	float sweepMul;
	float modFade;		/* current decaying part of the FM index, decays by modMul */
	float modMul;
	float noiseLp;		/* one-pole state for drum noise colouring */
	uint32_t age;		/* note-on order, for voice stealing */
};

struct Channel {
	uint8_t program;
	float volume;		/* CC7, 0..1 */
	float expression;	/* CC11, 0..1 */
	float panL, panR;	/* CC10, constant power */
	float bend;		/* frequency multiplier from pitch bend (+-2 semitones) */
	bool sustain;		/* CC64 */
};

struct Synth {
	int sampleRate;
	Voice voices[SYNTH_VOICES];
	Channel channels[SYNTH_CHANNELS];
	uint32_t noteCounter;
	uint32_t noise;		/* xorshift state */
};

void synth_init(Synth *synth, int sampleRate);

/* One MIDI channel message: status byte (0x80-0xEF) and its data bytes. */
void synth_message(Synth *synth, uint8_t status, uint8_t data1, uint8_t data2);

/* Release every sounding note (used when a song loops or stops). */
void synth_all_notes_off(Synth *synth);

/* Render `frames` stereo frames into out[frames * 2]. */
void synth_render(Synth *synth, int16_t *out, int frames);

#endif
