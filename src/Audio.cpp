#include "Audio.h"

#include "Midi.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Wolf3D-ish falloff: full volume up close, silent past MAX_DISTANCE tiles. */
static const float REFERENCE_DISTANCE = 2.0f;
static const float MAX_DISTANCE = 24.0f;

static const char *const SFX_PATHS[SFX_COUNT] = {
	"assets/sounds/door_open.wav",
	"assets/sounds/door_close.wav",
	"assets/sounds/pushwall.wav",
	"assets/sounds/pistol.wav",
	"assets/sounds/enemy_shot.wav",
	"assets/sounds/enemy_alert.wav",
	"assets/sounds/enemy_pain.wav",
	"assets/sounds/enemy_death.wav",
	"assets/sounds/player_hurt.wav",
	"assets/sounds/pickup.wav",
};
static_assert(sizeof(SFX_PATHS) / sizeof(SFX_PATHS[0]) == SFX_COUNT, "one path per Sfx");

#if RC_DEBUG
static void al_check_error(const char *call, const char *file, int line)
{
	ALenum err;
	while ((err = alGetError()) != AL_NO_ERROR)
		fprintf(stderr, "%s:%d: %s -> AL error 0x%04x\n", file, line, call, err);
}
#define AL_CHECK(call) do { call; al_check_error(#call, __FILE__, __LINE__); } while (0)
#else
#define AL_CHECK(call) do { call; } while (0)
#endif

/* ------------------------------------------------------------------------- */
/* WAV loader                                                                */
/* ------------------------------------------------------------------------- */

static uint32_t read_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_u16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

bool wav_load(Wav *wav, const char *path)
{
	memset(wav, 0, sizeof(*wav));

	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Cannot open %s\n", path);
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *file = (uint8_t *)malloc((size_t)size);
	if (!file || fread(file, 1, (size_t)size, f) != (size_t)size) {
		fprintf(stderr, "Cannot read %s\n", path);
		free(file);
		fclose(f);
		return false;
	}
	fclose(f);

	if (size < 12 || memcmp(file, "RIFF", 4) != 0 || memcmp(file + 8, "WAVE", 4) != 0) {
		fprintf(stderr, "%s: not a RIFF/WAVE file\n", path);
		free(file);
		return false;
	}

	/* Walk the chunks; anything that is not "fmt " or "data" is skipped. */
	bool haveFmt = false;
	const uint8_t *data = nullptr;
	uint32_t dataSize = 0;
	long pos = 12;
	while (pos + 8 <= size) {
		const uint8_t *chunk = file + pos;
		uint32_t chunkSize = read_u32(chunk + 4);
		const uint8_t *body = chunk + 8;
		if ((long)chunkSize > size - pos - 8)
			chunkSize = (uint32_t)(size - pos - 8);	/* truncated file: take what is there */

		if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
			uint16_t format = read_u16(body);
			wav->channels = read_u16(body + 2);
			wav->sampleRate = (int)read_u32(body + 4);
			wav->bits = read_u16(body + 14);
			if (format != 1) {
				fprintf(stderr, "%s: compressed WAV (format %u), only PCM is supported\n", path, format);
				free(file);
				return false;
			}
			haveFmt = true;
		} else if (memcmp(chunk, "data", 4) == 0) {
			data = body;
			dataSize = chunkSize;
		}
		pos += 8 + (long)chunkSize + (chunkSize & 1);	/* chunks are word aligned */
	}

	if (!haveFmt || !data) {
		fprintf(stderr, "%s: missing fmt or data chunk\n", path);
		free(file);
		return false;
	}
	if ((wav->channels != 1 && wav->channels != 2) || (wav->bits != 8 && wav->bits != 16)) {
		fprintf(stderr, "%s: %d channels at %d bits, need mono/stereo 8/16-bit\n",
			path, wav->channels, wav->bits);
		free(file);
		return false;
	}

	wav->size = dataSize - dataSize % (uint32_t)(wav->channels * wav->bits / 8);
	wav->data = (uint8_t *)malloc(wav->size ? wav->size : 1);
	memcpy(wav->data, data, wav->size);
	free(file);
	return true;
}

void wav_free(Wav *wav)
{
	free(wav->data);
	memset(wav, 0, sizeof(*wav));
}

static ALenum al_format(const Wav *wav)
{
	if (wav->channels == 1)
		return wav->bits == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
	return wav->bits == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
}

/* Upload a .wav into a new buffer. Positional sounds must be mono: OpenAL only spatialises mono. */
static bool load_buffer(const char *path, bool requireMono, ALuint *buffer)
{
	Wav wav;
	if (!wav_load(&wav, path))
		return false;
	if (requireMono && wav.channels != 1) {
		fprintf(stderr, "%s: positional sounds must be mono\n", path);
		wav_free(&wav);
		return false;
	}
	AL_CHECK(alGenBuffers(1, buffer));
	AL_CHECK(alBufferData(*buffer, al_format(&wav), wav.data, (ALsizei)wav.size, wav.sampleRate));
	wav_free(&wav);
	return true;
}

/* ------------------------------------------------------------------------- */
/* Device, sources                                                           */
/* ------------------------------------------------------------------------- */

bool audio_init(Audio *audio, float sfxVolume)
{
	memset(audio, 0, sizeof(*audio));
	audio->sfxGain = AUDIO_SFX_GAIN * sfxVolume;

	ALCdevice *device = alcOpenDevice(nullptr);
	if (!device) {
		fprintf(stderr, "No audio device, running silent\n");
		return true;
	}
	ALCcontext *context = alcCreateContext(device, nullptr);
	if (!context || !alcMakeContextCurrent(context)) {
		fprintf(stderr, "Cannot create an OpenAL context, running silent\n");
		if (context)
			alcDestroyContext(context);
		alcCloseDevice(device);
		return true;
	}
	audio->device = device;
	audio->context = context;
	audio->enabled = true;
	printf("Audio: %s\n", alcGetString(device, ALC_DEVICE_SPECIFIER));

	AL_CHECK(alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED));

	for (int i = 0; i < SFX_COUNT; i++) {
		if (!load_buffer(SFX_PATHS[i], true, &audio->buffers[i])) {
			audio_shutdown(audio);
			return false;
		}
	}

	AL_CHECK(alGenSources(AUDIO_SOURCES, audio->sources));
	for (int i = 0; i < AUDIO_SOURCES; i++) {
		ALuint s = audio->sources[i];
		AL_CHECK(alSourcef(s, AL_REFERENCE_DISTANCE, REFERENCE_DISTANCE));
		AL_CHECK(alSourcef(s, AL_MAX_DISTANCE, MAX_DISTANCE));
		AL_CHECK(alSourcef(s, AL_ROLLOFF_FACTOR, 1.0f));
		AL_CHECK(alSourcef(s, AL_GAIN, audio->sfxGain));
	}
	return true;
}

void audio_shutdown(Audio *audio)
{
	if (!audio->enabled)
		return;
	audio_stop_music(audio);
	if (audio->sources[0]) {
		for (int i = 0; i < AUDIO_SOURCES; i++)
			alSourceStop(audio->sources[i]);
		alDeleteSources(AUDIO_SOURCES, audio->sources);
	}
	for (int i = 0; i < SFX_COUNT; i++) {
		if (audio->buffers[i])
			alDeleteBuffers(1, &audio->buffers[i]);
	}
	alcMakeContextCurrent(nullptr);
	alcDestroyContext((ALCcontext *)audio->context);
	alcCloseDevice((ALCdevice *)audio->device);
	memset(audio, 0, sizeof(*audio));
}

void audio_set_listener(Audio *audio, glm::vec2 pos, float angle)
{
	if (!audio->enabled)
		return;
	/*
	 * at = (dir.x, 0, dir.y), up = +y. OpenAL's right is at x up = (-dir.y, 0, dir.x),
	 * which is the camera's right vector in map space: left/right panning matches the screen.
	 */
	float at[6] = { cosf(angle), 0.0f, sinf(angle), 0.0f, 1.0f, 0.0f };
	AL_CHECK(alListener3f(AL_POSITION, pos.x, 0.0f, pos.y));
	AL_CHECK(alListenerfv(AL_ORIENTATION, at));
}

/* A source that is not playing, or the next one round-robin if all are busy. */
static ALuint grab_source(Audio *audio)
{
	for (int i = 0; i < AUDIO_SOURCES; i++) {
		ALint state;
		alGetSourcei(audio->sources[i], AL_SOURCE_STATE, &state);
		if (state != AL_PLAYING)
			return audio->sources[i];
	}
	ALuint s = audio->sources[audio->nextSteal];
	audio->nextSteal = (audio->nextSteal + 1) % AUDIO_SOURCES;
	AL_CHECK(alSourceStop(s));
	return s;
}

void audio_play_at(Audio *audio, int sfx, glm::vec2 pos)
{
	if (!audio->enabled || sfx < 0 || sfx >= SFX_COUNT)
		return;
	ALuint s = grab_source(audio);
	AL_CHECK(alSourcei(s, AL_BUFFER, (ALint)audio->buffers[sfx]));
	AL_CHECK(alSource3f(s, AL_POSITION, pos.x, 0.0f, pos.y));
	AL_CHECK(alSourcePlay(s));
}

/* Render the next chunk of music into an OpenAL buffer and queue it. */
static void queue_music(Audio *audio, ALuint buffer)
{
	music_render(audio->music, audio->musicScratch, MUSIC_BUFFER_FRAMES);
	AL_CHECK(alBufferData(buffer, AL_FORMAT_STEREO16, audio->musicScratch,
		(ALsizei)sizeof(audio->musicScratch), MUSIC_RATE));
	AL_CHECK(alSourceQueueBuffers(audio->musicSource, 1, &buffer));
}

void audio_stop_music(Audio *audio)
{
	if (!audio->enabled || !audio->music)
		return;
	alSourceStop(audio->musicSource);
	alSourcei(audio->musicSource, AL_BUFFER, 0);	/* unqueue everything */
	alDeleteSources(1, &audio->musicSource);
	alDeleteBuffers(MUSIC_BUFFERS, audio->musicBuffers);
	music_close(audio->music);
	free(audio->music);
	audio->music = nullptr;
}

bool audio_play_music(Audio *audio, const char *path, const char *soundfont, float gain)
{
	if (!audio->enabled)
		return true;
	audio_stop_music(audio);	/* one song at a time */

	audio->music = (MusicPlayer *)malloc(sizeof(MusicPlayer));
	if (!audio->music || !music_open(audio->music, path, MUSIC_RATE, soundfont)) {
		free(audio->music);
		audio->music = nullptr;
		return false;
	}

	AL_CHECK(alGenSources(1, &audio->musicSource));
	AL_CHECK(alGenBuffers(MUSIC_BUFFERS, audio->musicBuffers));
	ALuint s = audio->musicSource;
	AL_CHECK(alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE));	/* glued to the listener */
	AL_CHECK(alSource3f(s, AL_POSITION, 0.0f, 0.0f, 0.0f));
	AL_CHECK(alSourcef(s, AL_ROLLOFF_FACTOR, 0.0f));
	AL_CHECK(alSourcef(s, AL_GAIN, gain));

	for (int i = 0; i < MUSIC_BUFFERS; i++)
		queue_music(audio, audio->musicBuffers[i]);
	AL_CHECK(alSourcePlay(s));
	return true;
}

void audio_update(Audio *audio)
{
	if (!audio->enabled || !audio->music)
		return;
	ALuint s = audio->musicSource;

	ALint processed = 0;
	alGetSourcei(s, AL_BUFFERS_PROCESSED, &processed);
	while (processed-- > 0) {
		ALuint buffer;
		AL_CHECK(alSourceUnqueueBuffers(s, 1, &buffer));
		queue_music(audio, buffer);
	}

	/* If we fell behind (window dragged, debugger) the source stops: restart it. */
	ALint state;
	alGetSourcei(s, AL_SOURCE_STATE, &state);
	if (state != AL_PLAYING)
		AL_CHECK(alSourcePlay(s));
}
