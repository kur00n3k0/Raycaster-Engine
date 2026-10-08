#include "Synth.h"

#include <fluidsynth.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * FluidSynth's own gain (default 0.2) is set so a full GM arrangement peaks
 * a few dB under full scale; the game scales music further with the OpenAL
 * source gain (MUSIC_GAIN * music_volume).
 */
static const double SYNTH_GAIN = 0.9;
static const int SYNTH_POLYPHONY = 128;

/* Directories scanned for the first *.sf2 (alphabetical) when none is given. */
static const char *SOUNDFONT_DIRS[] = {
	"assets/music",
	"/usr/share/soundfonts",
	"/usr/share/sounds/sf2",
	"/usr/local/share/soundfonts",
};

static bool has_sf2_suffix(const char *name)
{
	size_t n = strlen(name);
	return n > 4 && strcasecmp(name + n - 4, ".sf2") == 0;
}

/* First .sf2 in `dir` by name, written to out. False if there is none. */
static bool find_in_dir(const char *dir, char *out, size_t outSize)
{
	DIR *d = opendir(dir);
	if (!d)
		return false;
	char best[256] = "";
	while (dirent *e = readdir(d)) {
		if (!has_sf2_suffix(e->d_name) || strlen(e->d_name) >= sizeof(best))
			continue;
		if (!best[0] || strcmp(e->d_name, best) < 0)
			strcpy(best, e->d_name);
	}
	closedir(d);
	if (!best[0])
		return false;
	snprintf(out, outSize, "%s/%s", dir, best);
	return true;
}

static bool load_soundfont(Synth *synth, const char *path)
{
	if (access(path, R_OK) == 0 && fluid_is_soundfont(path) && fluid_synth_sfload(synth->fluid, path, 1) != FLUID_FAILED) {
		printf("Music: SoundFont %s\n", path);
		return true;
	}
	return false;
}

/* The configured file, else FluidSynth's compiled-in default, else the first one found. */
static bool load_any_soundfont(Synth *synth, const char *soundfont)
{
	if (soundfont && soundfont[0]) {
		if (load_soundfont(synth, soundfont))
			return true;
		fprintf(stderr, "Cannot load SoundFont %s\n", soundfont);
		return false;
	}

	char path[512];
	if (find_in_dir(SOUNDFONT_DIRS[0], path, sizeof(path)) && load_soundfont(synth, path))
		return true;

	char *def = nullptr;
	if (fluid_settings_dupstr(synth->settings, "synth.default-soundfont", &def) == FLUID_OK && def) {
		bool ok = load_soundfont(synth, def);
		fluid_free(def);
		if (ok)
			return true;
	}

	for (size_t i = 1; i < sizeof(SOUNDFONT_DIRS) / sizeof(SOUNDFONT_DIRS[0]); i++) {
		if (find_in_dir(SOUNDFONT_DIRS[i], path, sizeof(path)) && load_soundfont(synth, path))
			return true;
	}
	fprintf(stderr, "No General MIDI SoundFont (.sf2) found: set soundfont in raycaster.cfg "
		"or install one (e.g. soundfont-fluid)\n");
	return false;
}

bool synth_init(Synth *synth, int sampleRate, const char *soundfont)
{
	memset(synth, 0, sizeof(*synth));

	/* Keep FluidSynth's info/debug chatter off the console; warnings and errors stay. */
	fluid_set_log_function(FLUID_INFO, nullptr, nullptr);
	fluid_set_log_function(FLUID_DBG, nullptr, nullptr);

	/*
	 * new_fluid_settings() initialises every registered audio driver, which
	 * makes ALSA/SDL probe the sound cards and print noise. We never open a
	 * FluidSynth driver (OpenAL plays the samples), so register only the
	 * harmless "file" writer. Must happen before the first settings object.
	 */
	static const char *NO_DRIVERS[] = { "file", nullptr };
	fluid_audio_driver_register(NO_DRIVERS);

	synth->settings = new_fluid_settings();
	if (!synth->settings)
		return false;
	fluid_settings_setnum(synth->settings, "synth.sample-rate", (double)sampleRate);
	fluid_settings_setnum(synth->settings, "synth.gain", SYNTH_GAIN);
	fluid_settings_setint(synth->settings, "synth.polyphony", SYNTH_POLYPHONY);
	fluid_settings_setint(synth->settings, "synth.threadsafe-api", 0);	/* one thread drives it */

	synth->fluid = new_fluid_synth(synth->settings);
	if (!synth->fluid || !load_any_soundfont(synth, soundfont)) {
		synth_shutdown(synth);
		return false;
	}
	return true;
}

void synth_shutdown(Synth *synth)
{
	if (synth->fluid)
		delete_fluid_synth(synth->fluid);
	if (synth->settings)
		delete_fluid_settings(synth->settings);
	synth->fluid = nullptr;
	synth->settings = nullptr;
}

void synth_message(Synth *synth, uint8_t status, uint8_t data1, uint8_t data2)
{
	fluid_synth_t *fs = synth->fluid;
	int ch = status & 0x0F;
	switch (status & 0xF0) {
	case 0x80: fluid_synth_noteoff(fs, ch, data1); break;
	case 0x90:
		if (data2 == 0)
			fluid_synth_noteoff(fs, ch, data1);	/* velocity 0 = note-off */
		else
			fluid_synth_noteon(fs, ch, data1, data2);
		break;
	case 0xA0: fluid_synth_key_pressure(fs, ch, data1, data2); break;
	case 0xB0: fluid_synth_cc(fs, ch, data1, data2); break;
	case 0xC0: fluid_synth_program_change(fs, ch, data1); break;
	case 0xD0: fluid_synth_channel_pressure(fs, ch, data1); break;
	case 0xE0: fluid_synth_pitch_bend(fs, ch, data1 | (data2 << 7)); break;
	}
}

void synth_all_notes_off(Synth *synth)
{
	fluid_synth_all_notes_off(synth->fluid, -1);
}

void synth_render(Synth *synth, int16_t *out, int frames)
{
	/* Interleaved: left at out[0], right at out[1], stride 2. */
	fluid_synth_write_s16(synth->fluid, frames, out, 0, 2, out, 1, 2);
}
