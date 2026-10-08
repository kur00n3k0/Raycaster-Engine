#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#include <glm/vec2.hpp>

/*
 * OpenAL sound: one buffer per effect and a fixed pool of sources for
 * effects (digitized WAV, like Doom). Music is a MIDI file parsed by our
 * own loader and rendered by FluidSynth (Midi.h, Synth.h) and streamed through a queue of small buffers on one
 * non-positional source. World positions map to OpenAL as
 * (x, 0, y): the map lies in the x-z plane with +y up, so the listener's
 * "right" matches the camera's right (see audio_set_listener).
 *
 * If no audio device can be opened the game keeps running silently: every
 * call below is then a no-op.
 */

enum Sfx {
	SFX_DOOR_OPEN,
	SFX_DOOR_CLOSE,
	SFX_PUSHWALL,
	SFX_PISTOL,
	SFX_ENEMY_SHOT,
	SFX_ENEMY_ALERT,
	SFX_ENEMY_PAIN,
	SFX_ENEMY_DEATH,
	SFX_PLAYER_HURT,
	SFX_PICKUP,
	SFX_EXIT,
	SFX_EXPLODE,
	SFX_COUNT
};

enum {
	AUDIO_SOURCES = 16,
	MUSIC_RATE = 44100,
	MUSIC_BUFFERS = 4,		/* queued on the music source */
	MUSIC_BUFFER_FRAMES = 2048	/* ~46 ms each; ~186 ms of music queued */
};

/*
 * Mix balance at volume 1. Effects are short and loud (about -8 dB RMS in the
 * WAVs), the rendered music sits near -21 dB RMS, so effects are turned down
 * and the music is left at full gain: effects end up about 5 dB on top.
 * OpenAL clamps source gain to 1, so the music cannot go higher here; make it
 * louder with SYNTH_GAIN in Synth.cpp instead.
 */
static const float AUDIO_SFX_GAIN = 0.5f;
static const float AUDIO_MUSIC_GAIN = 1.0f;

struct MusicPlayer;

struct Audio {
	bool enabled;
	float sfxGain;
	void *device;		/* ALCdevice */
	void *context;		/* ALCcontext */
	unsigned buffers[SFX_COUNT];
	unsigned sources[AUDIO_SOURCES];
	int nextSteal;		/* round-robin victim when every source is busy */

	MusicPlayer *music;	/* null when no music is playing */
	unsigned musicBuffers[MUSIC_BUFFERS];
	unsigned musicSource;
	int16_t musicScratch[MUSIC_BUFFER_FRAMES * 2];
};

/* PCM sound decoded from a .wav file. */
struct Wav {
	int channels;		/* 1 or 2 */
	int bits;		/* 8 or 16 */
	int sampleRate;
	uint8_t *data;
	uint32_t size;		/* bytes */
};

/* RIFF/WAVE, uncompressed PCM only. Prints why and returns false otherwise. */
bool wav_load(Wav *wav, const char *path);
void wav_free(Wav *wav);

/* Opens the default device and loads assets/sounds. Returns false only on a broken asset. */
bool audio_init(Audio *audio, float sfxVolume);
void audio_shutdown(Audio *audio);

void audio_set_listener(Audio *audio, glm::vec2 pos, float angle);
void audio_play_at(Audio *audio, int sfx, glm::vec2 pos);

/*
 * Loop a .mid file as background music (non-positional), played by
 * FluidSynth with `soundfont` (null or "" = search for one). False if the
 * song or a SoundFont cannot be loaded; the game then runs without music.
 */
bool audio_play_music(Audio *audio, const char *path, const char *soundfont, float gain);

/* Stop and free the current song, if any. */
void audio_stop_music(Audio *audio);

/* Call once per frame: refills the music stream. */
void audio_update(Audio *audio);

#endif
