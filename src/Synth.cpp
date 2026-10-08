#include "Synth.h"

#include <math.h>
#include <string.h>

/*
 * An instrument. Every voice is carrier + modulator FM:
 *
 *   out = sin(carPhase + index(t) * sin(modPhase))     (phases in turns)
 *
 * with modulator frequency = carrier * modRatio and index(t) decaying from
 * modIndex towards modIndex * modSustain. Drums add noise and a pitch sweep.
 */
struct Patch {
	float modRatio;
	float modIndex;
	float modDecay;		/* 1/s, how fast the index falls (0 = constant) */
	float modSustain;	/* fraction of modIndex it falls to */
	float attack;		/* seconds to full level */
	float decay;		/* seconds to fall towards sustain */
	float sustain;		/* level held while the key is down; 0 = percussive */
	float release;		/* seconds to fade after note-off */
	float noise;		/* 0..1 white noise mix (drums) */
	float noiseTone;	/* one-pole low-pass coefficient for the noise, 1 = raw */
	float fixedHz;		/* > 0: ignore the note number (drums) */
	float sweep;		/* starting pitch multiplier - 1, decays away (drums) */
	float sweepDecay;	/* 1/s */
	float gain;
};

enum { STAGE_ATTACK, STAGE_DECAY, STAGE_SUSTAIN, STAGE_RELEASE };

/* Melodic patches, one per General MIDI family (program / 8). */
static const Patch FAMILY_PATCHES[16] = {
	/* ratio index mdecay msus   att    dec   sus   rel   noise tone fixed sweep sdec gain */
	{ 1.0f, 1.6f, 3.0f, 0.2f, 0.002f, 0.9f, 0.0f, 0.20f, 0, 1, 0, 0, 0, 0.9f },	/* piano */
	{ 3.5f, 2.0f, 2.5f, 0.1f, 0.001f, 1.0f, 0.0f, 0.30f, 0, 1, 0, 0, 0, 0.7f },	/* chromatic perc */
	{ 2.0f, 0.8f, 0.0f, 1.0f, 0.005f, 0.05f, 1.0f, 0.06f, 0, 1, 0, 0, 0, 0.6f },	/* organ */
	{ 1.0f, 2.2f, 4.0f, 0.2f, 0.002f, 1.2f, 0.0f, 0.10f, 0, 1, 0, 0, 0, 0.8f },	/* guitar */
	{ 1.0f, 2.0f, 5.0f, 0.4f, 0.003f, 0.40f, 0.5f, 0.05f, 0, 1, 0, 0, 0, 1.0f },	/* bass */
	{ 1.0f, 1.0f, 0.0f, 1.0f, 0.120f, 0.20f, 0.9f, 0.30f, 0, 1, 0, 0, 0, 0.5f },	/* strings */
	{ 1.0f, 1.2f, 0.0f, 1.0f, 0.150f, 0.20f, 0.9f, 0.40f, 0, 1, 0, 0, 0, 0.5f },	/* ensemble */
	{ 1.0f, 3.0f, 1.0f, 0.7f, 0.040f, 0.20f, 0.8f, 0.10f, 0, 1, 0, 0, 0, 0.6f },	/* brass */
	{ 2.0f, 1.5f, 0.0f, 1.0f, 0.020f, 0.10f, 0.9f, 0.08f, 0, 1, 0, 0, 0, 0.6f },	/* reed */
	{ 1.0f, 0.3f, 0.0f, 1.0f, 0.050f, 0.10f, 0.9f, 0.10f, 0, 1, 0, 0, 0, 0.6f },	/* pipe */
	{ 2.0f, 1.4f, 0.0f, 1.0f, 0.005f, 0.10f, 0.8f, 0.08f, 0, 1, 0, 0, 0, 0.6f },	/* synth lead: odd harmonics, square-ish */
	{ 1.0f, 0.8f, 0.0f, 1.0f, 0.300f, 0.30f, 0.9f, 0.60f, 0, 1, 0, 0, 0, 0.5f },	/* synth pad */
	{ 1.5f, 2.5f, 1.5f, 0.3f, 0.010f, 0.50f, 0.6f, 0.30f, 0, 1, 0, 0, 0, 0.6f },	/* synth fx */
	{ 3.0f, 1.8f, 3.0f, 0.2f, 0.002f, 0.60f, 0.0f, 0.20f, 0, 1, 0, 0, 0, 0.7f },	/* ethnic */
	{ 1.4f, 2.5f, 6.0f, 0.1f, 0.001f, 0.40f, 0.0f, 0.10f, 0, 1, 0, 0, 0, 0.8f },	/* percussive */
	{ 1.7f, 3.0f, 0.5f, 0.5f, 0.050f, 0.50f, 0.5f, 0.50f, 0.2f, 0.3f, 0, 0, 0, 0.5f },/* sound effects */
};

/* Drum kit for channel 10, picked by note number in drum_patch(). */
static const Patch DRUM_KICK  = { 1.0f, 0.0f, 0, 0, 0.001f, 0.25f, 0.0f, 0.05f, 0.05f, 0.1f, 50.0f, 2.5f, 30.0f, 1.4f };
static const Patch DRUM_SNARE = { 1.0f, 0.0f, 0, 0, 0.001f, 0.18f, 0.0f, 0.05f, 0.75f, 0.8f, 190.0f, 0.3f, 40.0f, 0.9f };
static const Patch DRUM_HAT   = { 1.0f, 0.0f, 0, 0, 0.001f, 0.05f, 0.0f, 0.02f, 1.0f, 1.0f, 0.0f, 0, 0, 0.35f };
static const Patch DRUM_OPEN  = { 1.0f, 0.0f, 0, 0, 0.001f, 0.30f, 0.0f, 0.10f, 1.0f, 1.0f, 0.0f, 0, 0, 0.30f };
static const Patch DRUM_CRASH = { 1.0f, 0.0f, 0, 0, 0.002f, 1.20f, 0.0f, 0.30f, 1.0f, 0.7f, 0.0f, 0, 0, 0.35f };
static const Patch DRUM_TOM   = { 1.0f, 0.0f, 0, 0, 0.001f, 0.30f, 0.0f, 0.05f, 0.1f, 0.2f, 0.0f, 0.5f, 12.0f, 1.0f };

static const Patch *drum_patch(uint8_t note)
{
	switch (note) {
	case 35: case 36:			return &DRUM_KICK;
	case 37: case 38: case 39: case 40:	return &DRUM_SNARE;
	case 42: case 44:			return &DRUM_HAT;
	case 46:				return &DRUM_OPEN;
	case 49: case 51: case 52: case 55: case 57: case 59:
						return &DRUM_CRASH;
	case 41: case 43: case 45: case 47: case 48: case 50:
						return &DRUM_TOM;
	default:				return &DRUM_SNARE;
	}
}

/* ------------------------------------------------------------------------- */
/* Tables                                                                    */
/* ------------------------------------------------------------------------- */

enum { SINE_BITS = 12, SINE_SIZE = 1 << SINE_BITS };
static float g_sine[SINE_SIZE];
static bool g_sineReady = false;

static void build_sine()
{
	if (g_sineReady)
		return;
	for (int i = 0; i < SINE_SIZE; i++)
		g_sine[i] = sinf(6.28318530718f * (float)i / (float)SINE_SIZE);
	g_sineReady = true;
}

/* sin(2 * pi * turns) by table lookup; any real number of turns. */
static inline float sine(float turns)
{
	float f = turns - floorf(turns);
	return g_sine[(int)(f * (float)SINE_SIZE) & (SINE_SIZE - 1)];
}

static float note_hz(int note)
{
	return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

/* ------------------------------------------------------------------------- */
/* Messages                                                                  */
/* ------------------------------------------------------------------------- */

static void set_pan(Channel *c, uint8_t value)
{
	float p = (float)value / 127.0f * 1.57079633f;	/* 0 = left, 64 = centre, 127 = right */
	c->panL = cosf(p);
	c->panR = sinf(p);
}

static void reset_channel(Channel *c)
{
	c->program = 0;
	c->volume = 100.0f / 127.0f;
	c->expression = 1.0f;
	set_pan(c, 64);
	c->bend = 1.0f;
	c->sustain = false;
}

void synth_init(Synth *synth, int sampleRate)
{
	build_sine();
	memset(synth, 0, sizeof(*synth));
	synth->sampleRate = sampleRate;
	synth->noise = 0x1234567u;
	for (int i = 0; i < SYNTH_CHANNELS; i++)
		reset_channel(&synth->channels[i]);
}

static void release_voice(Voice *v)
{
	v->released = true;
	v->held = false;
	v->stage = STAGE_RELEASE;
}

/* Free voice, else the quietest releasing one, else the oldest. */
static Voice *allocate_voice(Synth *synth)
{
	Voice *best = nullptr;
	for (int i = 0; i < SYNTH_VOICES; i++) {
		Voice *v = &synth->voices[i];
		if (!v->active)
			return v;
	}
	for (int i = 0; i < SYNTH_VOICES; i++) {
		Voice *v = &synth->voices[i];
		if (v->released && (!best || v->env < best->env))
			best = v;
	}
	if (best)
		return best;
	best = &synth->voices[0];
	for (int i = 1; i < SYNTH_VOICES; i++) {
		if (synth->voices[i].age < best->age)
			best = &synth->voices[i];
	}
	return best;
}

static void note_on(Synth *synth, uint8_t ch, uint8_t note, uint8_t velocity)
{
	const Channel *c = &synth->channels[ch];
	const Patch *patch = ch == SYNTH_DRUM_CHANNEL ? drum_patch(note) : &FAMILY_PATCHES[c->program / 8];

	Voice *v = allocate_voice(synth);
	memset(v, 0, sizeof(*v));
	v->active = true;
	v->channel = ch;
	v->note = note;
	v->patch = patch;
	v->baseHz = patch->fixedHz > 0.0f ? patch->fixedHz : note_hz(note);
	if (ch == SYNTH_DRUM_CHANNEL && patch == &DRUM_TOM)
		v->baseHz = note_hz(note) * 0.5f;
	float vel = (float)velocity / 127.0f;
	v->gain = vel * vel;
	v->stage = STAGE_ATTACK;
	v->age = synth->noteCounter++;

	/* Exponential segments: about -60 dB (x0.001) over the patch's decay/release time. */
	const float dt = 1.0f / (float)synth->sampleRate;
	v->attackStep = dt / patch->attack;
	v->decayMul = expf(-6.9f * dt / patch->decay);
	v->releaseMul = expf(-6.9f * dt / patch->release);
	v->sweep = patch->sweep;
	v->sweepMul = expf(-patch->sweepDecay * dt);
	v->modFade = 1.0f;
	v->modMul = expf(-patch->modDecay * dt);
}

static void note_off(Synth *synth, uint8_t ch, uint8_t note)
{
	for (int i = 0; i < SYNTH_VOICES; i++) {
		Voice *v = &synth->voices[i];
		if (!v->active || v->released || v->channel != ch || v->note != note)
			continue;
		if (synth->channels[ch].sustain)
			v->held = true;
		else
			release_voice(v);
	}
}

static void control_change(Synth *synth, uint8_t ch, uint8_t cc, uint8_t value)
{
	Channel *c = &synth->channels[ch];
	switch (cc) {
	case 7:  c->volume = (float)value / 127.0f; break;
	case 10: set_pan(c, value); break;
	case 11: c->expression = (float)value / 127.0f; break;
	case 64:
		c->sustain = value >= 64;
		if (!c->sustain) {
			for (int i = 0; i < SYNTH_VOICES; i++) {
				Voice *v = &synth->voices[i];
				if (v->active && v->held && v->channel == ch)
					release_voice(v);
			}
		}
		break;
	case 120:	/* all sound off */
	case 123:	/* all notes off */
		for (int i = 0; i < SYNTH_VOICES; i++) {
			Voice *v = &synth->voices[i];
			if (v->active && v->channel == ch)
				release_voice(v);
		}
		break;
	case 121:	/* reset all controllers */
		{
			uint8_t program = c->program;
			reset_channel(c);
			c->program = program;
		}
		break;
	default:
		break;
	}
}

void synth_message(Synth *synth, uint8_t status, uint8_t data1, uint8_t data2)
{
	uint8_t ch = status & 0x0F;
	switch (status & 0xF0) {
	case 0x80:
		note_off(synth, ch, data1);
		break;
	case 0x90:
		if (data2 == 0)
			note_off(synth, ch, data1);	/* note-on with velocity 0 is a note-off */
		else
			note_on(synth, ch, data1, data2);
		break;
	case 0xB0:
		control_change(synth, ch, data1, data2);
		break;
	case 0xC0:
		synth->channels[ch].program = data1 & 0x7F;
		break;
	case 0xE0:
		{
			int value = (data1 | (data2 << 7)) - 8192;	/* 14-bit, centre 8192 */
			synth->channels[ch].bend = powf(2.0f, (float)value / 8192.0f * 2.0f / 12.0f);
		}
		break;
	default:
		break;	/* aftertouch: ignored */
	}
}

void synth_all_notes_off(Synth *synth)
{
	for (int i = 0; i < SYNTH_VOICES; i++) {
		if (synth->voices[i].active)
			release_voice(&synth->voices[i]);
	}
	for (int i = 0; i < SYNTH_CHANNELS; i++)
		synth->channels[i].sustain = false;
}

/* ------------------------------------------------------------------------- */
/* Rendering                                                                 */
/* ------------------------------------------------------------------------- */

static inline float white(Synth *synth)
{
	uint32_t s = synth->noise;
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	synth->noise = s;
	return (float)(s & 0xFFFF) / 32767.5f - 1.0f;
}

/* Advance the envelope one sample and return the level. Deactivates finished voices. */
static float step_envelope(Voice *v)
{
	const Patch *p = v->patch;
	switch (v->stage) {
	case STAGE_ATTACK:
		v->env += v->attackStep;
		if (v->env >= 1.0f) {
			v->env = 1.0f;
			v->stage = STAGE_DECAY;
		}
		break;
	case STAGE_DECAY:
		v->env = p->sustain + (v->env - p->sustain) * v->decayMul;
		if (p->sustain <= 0.0f && v->env < 0.001f)
			v->active = false;
		break;
	case STAGE_RELEASE:
		v->env *= v->releaseMul;
		if (v->env < 0.001f)
			v->active = false;
		break;
	default:
		break;
	}
	return v->env;
}

void synth_render(Synth *synth, int16_t *out, int frames)
{
	const float dt = 1.0f / (float)synth->sampleRate;
	const float MASTER = 0.5f;

	for (int f = 0; f < frames; f++) {
		float left = 0.0f, right = 0.0f;

		for (int i = 0; i < SYNTH_VOICES; i++) {
			Voice *v = &synth->voices[i];
			if (!v->active)
				continue;
			const Patch *p = v->patch;
			const Channel *c = &synth->channels[v->channel];

			float env = step_envelope(v);
			if (!v->active)
				continue;

			float hz = v->baseHz * c->bend * (1.0f + v->sweep);
			v->sweep *= v->sweepMul;

			float index = p->modIndex * (p->modSustain + (1.0f - p->modSustain) * v->modFade);
			v->modFade *= v->modMul;

			float mod = sine(v->modPhase);
			float s = sine(v->carPhase + index * mod * 0.159154943f);	/* index in radians -> turns */
			if (p->noise > 0.0f) {
				v->noiseLp += p->noiseTone * (white(synth) - v->noiseLp);
				s = s * (1.0f - p->noise) + v->noiseLp * p->noise;
			}

			v->carPhase += hz * dt;
			v->modPhase += hz * p->modRatio * dt;
			v->carPhase -= floorf(v->carPhase);
			v->modPhase -= floorf(v->modPhase);

			float amp = s * env * v->gain * p->gain * c->volume * c->expression;
			left += amp * c->panL;
			right += amp * c->panR;
		}

		/* Soft clip so a busy chord bends instead of wrapping. */
		left = tanhf(left * MASTER);
		right = tanhf(right * MASTER);
		out[f * 2 + 0] = (int16_t)lrintf(left * 32000.0f);
		out[f * 2 + 1] = (int16_t)lrintf(right * 32000.0f);
	}
}
