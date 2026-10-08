/*
 * Generates the placeholder audio. Run from the repo root:
 *
 *     ./build/gen_sounds
 *
 *   assets/sounds/ (.wav) effects, synthesised here as 22050 Hz mono 16-bit PCM
 *   assets/music/ (.mid)   music, written as Standard MIDI Files (format 1):
 *                          e1m1.mid (driving, A minor), e1m2.mid (slow, D minor).
 *                          Maps pick one with @music.
 *
 * The output is checked in; rerun only to change it.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

enum { RATE = 22050 };

static const float PI = 3.14159265f;

struct Buffer {
	float *s;
	int count;
};

static Buffer make_buffer(float seconds)
{
	Buffer b;
	b.count = (int)(seconds * RATE);
	b.s = (float *)calloc((size_t)b.count, sizeof(float));
	return b;
}

/* Deterministic white noise in [-1, 1]. */
static uint32_t g_seed = 0x2545F491u;
static float white()
{
	g_seed ^= g_seed << 13;
	g_seed ^= g_seed >> 17;
	g_seed ^= g_seed << 5;
	return (float)(g_seed & 0xFFFF) / 32767.5f - 1.0f;
}

static float square(float phase, float duty)
{
	return (phase - floorf(phase)) < duty ? 1.0f : -1.0f;
}

static float saw(float phase)
{
	return 2.0f * (phase - floorf(phase)) - 1.0f;
}

/* Linear attack, linear release, flat between. Keeps note edges click-free. */
static float envelope(float t, float length, float attack, float release)
{
	if (t < attack)
		return t / attack;
	if (t > length - release)
		return fmaxf(0.0f, (length - t) / release);
	return 1.0f;
}

/* ------------------------------------------------------------------------- */
/* Effects                                                                   */
/* ------------------------------------------------------------------------- */

/* Pneumatic slide: low-passed noise hiss over a motor hum that rises a little. */
static Buffer sfx_door_open()
{
	Buffer b = make_buffer(0.65f);
	float lp = 0.0f, phase = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		lp += 0.18f * (white() - lp);
		phase += (85.0f + 40.0f * t) / RATE;
		float env = envelope(t, 0.65f, 0.03f, 0.25f);
		b.s[i] = env * (0.55f * lp * 2.0f + 0.35f * square(phase, 0.3f));
	}
	return b;
}

/* Short slide followed by a heavy clunk. */
static Buffer sfx_door_close()
{
	Buffer b = make_buffer(0.6f);
	float lp = 0.0f, phase = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		float v = 0.0f;
		if (t < 0.4f) {
			lp += 0.15f * (white() - lp);
			phase += (120.0f - 50.0f * t) / RATE;
			v = envelope(t, 0.4f, 0.02f, 0.05f) * (lp * 1.1f + 0.3f * square(phase, 0.3f));
		}
		if (t >= 0.38f) {
			float k = t - 0.38f;
			float decay = expf(-k * 22.0f);
			v += decay * (0.9f * sinf(2.0f * PI * 65.0f * k) + 0.4f * white() * expf(-k * 60.0f));
		}
		b.s[i] = v;
	}
	return b;
}

/* Stone grinding on stone: rumbling low saw plus scraping noise in bursts. */
static Buffer sfx_pushwall()
{
	Buffer b = make_buffer(1.4f);
	float lp = 0.0f, phase = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		lp += 0.08f * (white() - lp);
		phase += (48.0f + 6.0f * sinf(t * 9.0f)) / RATE;
		float scrape = 0.6f + 0.4f * sinf(2.0f * PI * 7.0f * t) * sinf(2.0f * PI * 3.1f * t);
		float env = envelope(t, 1.4f, 0.05f, 0.3f);
		b.s[i] = env * (0.45f * saw(phase) + 1.8f * lp * scrape);
	}
	return b;
}

/* Pistol: a sharp crack of bright noise over a short low thump. */
static Buffer sfx_pistol()
{
	Buffer b = make_buffer(0.35f);
	float lp = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		lp += 0.5f * (white() - lp);
		float crack = lp * expf(-t * 28.0f);
		float thump = sinf(2.0f * PI * (90.0f + 200.0f * expf(-t * 40.0f)) * t) * expf(-t * 18.0f);
		b.s[i] = 1.2f * crack + 0.8f * thump;
	}
	return b;
}

/* Enemy rifle: deeper and longer than the pistol so the two are told apart. */
static Buffer sfx_enemy_shot()
{
	Buffer b = make_buffer(0.5f);
	float lp = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		lp += 0.25f * (white() - lp);
		float blast = lp * expf(-t * 14.0f);
		float thump = sinf(2.0f * PI * (60.0f + 120.0f * expf(-t * 30.0f)) * t) * expf(-t * 10.0f);
		b.s[i] = 1.5f * blast + 0.9f * thump;
	}
	return b;
}

/* Alert bark: two quick rising buzzy syllables, a synthetic "hey-hup!". */
static Buffer sfx_enemy_alert()
{
	Buffer b = make_buffer(0.42f);
	float phase = 0.0f, lp = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		bool second = t >= 0.2f;
		float local = second ? t - 0.2f : t;
		float hz = second ? 260.0f + 160.0f * local : 200.0f + 220.0f * local;
		phase += hz / RATE;
		float voice = saw(phase);
		lp += 0.3f * (voice - lp);			/* soften into a vowel-ish buzz */
		b.s[i] = lp * envelope(local, 0.2f, 0.01f, 0.06f);
	}
	return b;
}

/* Pain grunt: short falling buzz with breath noise. */
static Buffer sfx_enemy_pain()
{
	Buffer b = make_buffer(0.28f);
	float phase = 0.0f, lp = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		phase += (230.0f - 300.0f * t) / RATE;
		lp += 0.25f * (saw(phase) + 0.4f * white() - lp);
		b.s[i] = lp * envelope(t, 0.28f, 0.01f, 0.12f);
	}
	return b;
}

/* Death cry: a long falling groan that ends in a thud. */
static Buffer sfx_enemy_death()
{
	Buffer b = make_buffer(0.9f);
	float phase = 0.0f, lp = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		phase += (250.0f * expf(-t * 1.8f) + 50.0f) / RATE;
		lp += 0.2f * (saw(phase) + 0.3f * white() - lp);
		float v = lp * envelope(t, 0.75f, 0.02f, 0.3f);
		if (t > 0.7f) {
			float k = t - 0.7f;
			v += 0.8f * sinf(2.0f * PI * 55.0f * k) * expf(-k * 25.0f);
		}
		b.s[i] = v;
	}
	return b;
}

/* Player hurt: low, short "oof". */
static Buffer sfx_player_hurt()
{
	Buffer b = make_buffer(0.3f);
	float phase = 0.0f, lp = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		phase += (150.0f - 120.0f * t) / RATE;
		lp += 0.15f * (saw(phase) + 0.5f * white() - lp);
		b.s[i] = lp * envelope(t, 0.3f, 0.005f, 0.15f);
	}
	return b;
}

/* Pickup: bright two-note blip, the universal "got it". */
static Buffer sfx_pickup()
{
	Buffer b = make_buffer(0.2f);
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		float hz = t < 0.08f ? 880.0f : 1320.0f;
		float local = t < 0.08f ? t : t - 0.08f;
		float len = t < 0.08f ? 0.08f : 0.12f;
		b.s[i] = 0.6f * square(hz * t, 0.5f) * envelope(local, len, 0.003f, 0.03f);
	}
	return b;
}

/* Exit switch: a clunk, then a two-note elevator chime (sines with a decaying tail). */
static Buffer sfx_exit()
{
	Buffer b = make_buffer(1.1f);
	const float PI2 = 6.2831853f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		float v = 0.0f;
		if (t < 0.06f)
			v += 0.7f * square(90.0f * t, 0.5f) * envelope(t, 0.06f, 0.002f, 0.04f);
		static const float START[2] = { 0.12f, 0.42f };
		static const float HZ[2] = { 659.25f, 523.25f };	/* E5, C5: ding-dong */
		for (int n = 0; n < 2; n++) {
			float lt = t - START[n];
			if (lt < 0.0f)
				continue;
			float env = fminf(1.0f, lt / 0.005f) * expf(-lt * 4.0f);
			v += 0.45f * env * (sinf(PI2 * HZ[n] * lt) + 0.3f * sinf(PI2 * HZ[n] * 2.0f * lt));
		}
		b.s[i] = v;
	}
	return b;
}

/*
 * Barrel explosion: a deep thump sweeping down under a roar of noise whose
 * low-pass closes as it dies away, with a few crackles in the tail.
 */
static Buffer sfx_explode()
{
	Buffer b = make_buffer(1.6f);
	float lp = 0.0f, lp2 = 0.0f, phase = 0.0f;
	for (int i = 0; i < b.count; i++) {
		float t = (float)i / RATE;
		float cutoff = 0.02f + 0.5f * expf(-t * 6.0f);
		lp += cutoff * (white() - lp);
		lp2 += cutoff * (lp - lp2);
		phase += (35.0f + 90.0f * expf(-t * 12.0f)) / RATE;
		float thump = sinf(2.0f * PI * phase) * expf(-t * 5.0f);
		float roar = 3.0f * lp2 * (fminf(1.0f, t / 0.004f)) * expf(-t * 2.2f);
		float crackle = (t > 0.25f && white() > 0.997f) ? 0.5f * expf(-(t - 0.25f) * 3.0f) : 0.0f;
		b.s[i] = 1.1f * thump + roar + crackle;
	}
	return b;
}

/* ------------------------------------------------------------------------- */
/* Music: an 8-bar minor-key loop as a Standard MIDI File                    */
/* ------------------------------------------------------------------------- */

enum { PPQ = 96, EIGHTH = PPQ / 2, BAR = PPQ * 4, BARS = 8 };

struct MidiOut {
	uint32_t tick;
	uint32_t order;
	std::vector<uint8_t> bytes;	/* status + data, or 0xFF meta */
};

struct Track {
	std::vector<MidiOut> events;

	void add(uint32_t tick, std::initializer_list<uint8_t> bytes)
	{
		MidiOut e;
		e.tick = tick;
		e.order = (uint32_t)events.size();
		e.bytes = bytes;
		events.push_back(e);
	}

	void note(uint32_t tick, uint32_t length, uint8_t ch, uint8_t key, uint8_t velocity)
	{
		add(tick, { (uint8_t)(0x90 | ch), key, velocity });
		add(tick + length, { (uint8_t)(0x80 | ch), key, 0 });
	}
};

static void put_vlq(std::vector<uint8_t> *out, uint32_t v)
{
	uint8_t tmp[5];
	int n = 0;
	tmp[n++] = (uint8_t)(v & 0x7F);
	while (v >>= 7)
		tmp[n++] = (uint8_t)(0x80 | (v & 0x7F));
	while (n--)
		out->push_back(tmp[n]);
}

static void put_be(std::vector<uint8_t> *out, uint32_t v, int bytes)
{
	for (int i = bytes - 1; i >= 0; i--)
		out->push_back((uint8_t)(v >> (i * 8)));
}

/* Sort by tick (note-offs first at equal ticks), delta-encode, end with end-of-track at `endTick`. */
static std::vector<uint8_t> encode_track(Track *t, uint32_t endTick)
{
	std::stable_sort(t->events.begin(), t->events.end(), [](const MidiOut &a, const MidiOut &b) {
		if (a.tick != b.tick)
			return a.tick < b.tick;
		bool offA = (a.bytes[0] & 0xF0) == 0x80, offB = (b.bytes[0] & 0xF0) == 0x80;
		if (offA != offB)
			return offA;
		return a.order < b.order;
	});
	std::vector<uint8_t> out;
	uint32_t now = 0;
	for (const MidiOut &e : t->events) {
		put_vlq(&out, e.tick - now);
		now = e.tick;
		out.insert(out.end(), e.bytes.begin(), e.bytes.end());
	}
	put_vlq(&out, endTick - now);
	out.push_back(0xFF);
	out.push_back(0x2F);
	out.push_back(0x00);
	return out;
}

/*
 * One 8-bar loop: bars 1-4 arpeggiate the chords on the lead, bars 5-8 play
 * the melody; bass on roots and octaves, a pad holding each chord, drums.
 */
struct SongDef {
	const char *path;
	int bpm;
	int roots[BARS];		/* bass note per bar */
	int chords[BARS][4];		/* lead arpeggio / pad notes per bar */
	int melody[32];			/* eighths for bars 5-8, 0 = rest */
	uint8_t leadProgram, bassProgram, padProgram;	/* General MIDI, 0-based */
	bool halfTime;			/* snare on beat 3 only, sparser hats */
};

static const SongDef SONGS[] = {
	{
		"assets/music/e1m1.mid", 132,
		/* Chord per bar: Am F G Em | Am F G E */
		{ 45, 41, 43, 40, 45, 41, 43, 40 },
		{
			{ 69, 72, 76, 81 }, { 65, 69, 72, 77 }, { 67, 71, 74, 79 }, { 64, 67, 71, 76 },
			{ 69, 72, 76, 81 }, { 65, 69, 72, 77 }, { 67, 71, 74, 79 }, { 64, 68, 71, 76 },
		},
		{
			76, 0, 76, 74, 72, 0, 69, 72,
			77, 0, 76, 74, 72, 0, 69, 0,
			74, 0, 74, 76, 79, 0, 77, 76,
			76, 0, 74, 0, 71, 0, 68, 0,
		},
		80, 38, 89,	/* Lead 1 (square), Synth Bass 1, Pad 2 (warm) */
		false,
	},
	{
		"assets/music/e1m2.mid", 96,
		/* Chord per bar: Dm Bb C A | Dm Gm Bb A */
		{ 38, 34, 36, 33, 38, 43, 34, 33 },
		{
			{ 62, 65, 69, 74 }, { 58, 62, 65, 70 }, { 60, 64, 67, 72 }, { 57, 61, 64, 69 },
			{ 62, 65, 69, 74 }, { 55, 58, 62, 67 }, { 58, 62, 65, 70 }, { 57, 61, 64, 69 },
		},
		{
			74, 0, 0, 72, 69, 0, 65, 0,
			67, 0, 0, 65, 62, 0, 0, 0,
			65, 0, 67, 69, 70, 0, 69, 67,
			69, 0, 0, 0, 61, 0, 64, 0,
		},
		11, 33, 91,	/* Vibraphone, Electric Bass (finger), Pad 4 (choir) */
		true,
	},
};

static bool write_music(const SongDef *song)
{
	const char *path = song->path;
	const int BPM = song->bpm;
	const uint32_t END = BAR * BARS;
	const int *roots = song->roots;
	const int (*chords)[4] = song->chords;
	const int *melody = song->melody;
	static const int arp[8] = { 0, 1, 2, 3, 2, 1, 0, 2 };

	Track tempo, lead, bass, pad, drums;

	uint32_t us = 60000000u / BPM;
	tempo.add(0, { 0xFF, 0x51, 0x03, (uint8_t)(us >> 16), (uint8_t)(us >> 8), (uint8_t)us });
	tempo.add(0, { 0xFF, 0x58, 0x04, 4, 2, 24, 8 });	/* 4/4 */

	/* Channel setup: program, volume (CC7), pan (CC10). */
	lead.add(0, { 0xC0, song->leadProgram });
	lead.add(0, { 0xB0, 7, 100 });
	lead.add(0, { 0xB0, 10, 76 });
	bass.add(0, { 0xC1, song->bassProgram });
	bass.add(0, { 0xB1, 7, 110 });
	bass.add(0, { 0xB1, 10, 56 });
	pad.add(0, { 0xC2, song->padProgram });
	pad.add(0, { 0xB2, 7, 70 });
	pad.add(0, { 0xB2, 10, 64 });

	for (int step = 0; step < BARS * 8; step++) {
		int bar = step / 8;
		int beat8 = step % 8;
		uint32_t tick = (uint32_t)step * EIGHTH;

		int key = bar < 4 ? chords[bar][arp[beat8]] : melody[step - 32];
		if (key)
			lead.note(tick, EIGHTH - 6, 0, (uint8_t)key, beat8 % 2 ? 84 : 100);

		int bassKey = roots[bar] + (beat8 % 2 ? 12 : 0);
		bass.note(tick, EIGHTH - 4, 1, (uint8_t)bassKey, beat8 % 2 ? 80 : 105);

		if (beat8 == 0) {
			for (int i = 0; i < 3; i++)
				pad.note(tick, BAR - 8, 2, (uint8_t)(chords[bar][i] - 12), 70);
		}

		if (song->halfTime) {
			if (beat8 == 0 || beat8 == 5)
				drums.note(tick, EIGHTH / 2, 9, 36, 110);	/* kick */
			if (beat8 == 4)
				drums.note(tick, EIGHTH / 2, 9, 38, 95);	/* snare */
			if (beat8 % 2 == 0)
				drums.note(tick, EIGHTH / 2, 9, 42, 55);	/* closed hat */
		} else {
			if (beat8 == 0 || beat8 == 4)
				drums.note(tick, EIGHTH / 2, 9, 36, 115);	/* kick */
			if (beat8 == 2 || beat8 == 6)
				drums.note(tick, EIGHTH / 2, 9, 38, 100);	/* snare */
			drums.note(tick, EIGHTH / 2, 9, 42, beat8 % 2 ? 90 : 60);	/* closed hat */
		}
		if (step == 0 || step == 32)
			drums.note(tick, EIGHTH, 9, 49, 90);		/* crash */
	}

	std::vector<uint8_t> file;
	file.insert(file.end(), { 'M', 'T', 'h', 'd' });
	put_be(&file, 6, 4);
	put_be(&file, 1, 2);		/* format 1 */
	put_be(&file, 5, 2);		/* tracks */
	put_be(&file, PPQ, 2);
	Track *tracks[] = { &tempo, &lead, &bass, &pad, &drums };
	for (Track *t : tracks) {
		std::vector<uint8_t> body = encode_track(t, END);
		file.insert(file.end(), { 'M', 'T', 'r', 'k' });
		put_be(&file, (uint32_t)body.size(), 4);
		file.insert(file.end(), body.begin(), body.end());
	}

	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "Cannot write %s\n", path);
		return false;
	}
	fwrite(file.data(), 1, file.size(), f);
	fclose(f);
	printf("wrote %s (%d bars at %d BPM)\n", path, BARS, BPM);
	return true;
}

/* ------------------------------------------------------------------------- */
/* WAV writer                                                                */
/* ------------------------------------------------------------------------- */

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

/* Normalise to `peak`, soft-clip, write 16-bit mono PCM. */
static bool write_wav(const char *path, Buffer b, float peak)
{
	float maxAbs = 1e-6f;
	for (int i = 0; i < b.count; i++)
		maxAbs = fmaxf(maxAbs, fabsf(b.s[i]));

	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "Cannot write %s\n", path);
		return false;
	}
	uint32_t dataBytes = (uint32_t)b.count * 2;
	fwrite("RIFF", 1, 4, f);
	put_u32(f, 36 + dataBytes);
	fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f);
	put_u32(f, 16);
	put_u16(f, 1);			/* PCM */
	put_u16(f, 1);			/* mono */
	put_u32(f, RATE);
	put_u32(f, RATE * 2);		/* byte rate */
	put_u16(f, 2);			/* block align */
	put_u16(f, 16);			/* bits */
	fwrite("data", 1, 4, f);
	put_u32(f, dataBytes);
	for (int i = 0; i < b.count; i++) {
		float v = tanhf(b.s[i] / maxAbs * 1.2f) / tanhf(1.2f) * peak;
		put_u16(f, (uint16_t)(int16_t)lrintf(v * 32767.0f));
	}
	fclose(f);
	return true;
}

int main()
{
	struct Entry { const char *path; Buffer (*make)(); float peak; };
	const Entry entries[] = {
		{ "assets/sounds/door_open.wav", sfx_door_open, 0.8f },
		{ "assets/sounds/door_close.wav", sfx_door_close, 0.9f },
		{ "assets/sounds/pushwall.wav", sfx_pushwall, 0.8f },
		{ "assets/sounds/pistol.wav", sfx_pistol, 0.9f },
		{ "assets/sounds/enemy_shot.wav", sfx_enemy_shot, 0.9f },
		{ "assets/sounds/enemy_alert.wav", sfx_enemy_alert, 0.8f },
		{ "assets/sounds/enemy_pain.wav", sfx_enemy_pain, 0.8f },
		{ "assets/sounds/enemy_death.wav", sfx_enemy_death, 0.85f },
		{ "assets/sounds/player_hurt.wav", sfx_player_hurt, 0.8f },
		{ "assets/sounds/pickup.wav", sfx_pickup, 0.6f },
		{ "assets/sounds/exit.wav", sfx_exit, 0.8f },
		{ "assets/sounds/explode.wav", sfx_explode, 0.95f },
	};
	for (const Entry &e : entries) {
		Buffer b = e.make();
		bool ok = write_wav(e.path, b, e.peak);
		free(b.s);
		if (!ok)
			return 1;
		printf("wrote %s (%.2f s)\n", e.path, (double)b.count / RATE);
	}
	for (const SongDef &song : SONGS) {
		if (!write_music(&song))
			return 1;
	}
	return 0;
}
