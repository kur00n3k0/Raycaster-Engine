#ifndef CONFIG_H
#define CONFIG_H

/*
 * Settings from a plain-text file (raycaster.cfg by default), one
 * "name = value" per line, '#' starts a comment. Unknown names and bad
 * values are reported with their line number and keep the default.
 * The game is keyboard only; every action takes up to two key names.
 */

enum Action {
	ACT_FORWARD,
	ACT_BACK,
	ACT_STRAFE_LEFT,
	ACT_STRAFE_RIGHT,
	ACT_TURN_LEFT,
	ACT_TURN_RIGHT,
	ACT_RUN,
	ACT_USE,
	ACT_FIRE,
	ACT_WEAPON_FISTS,
	ACT_WEAPON_PISTOL,
	ACT_WEAPON_SMG,
	ACT_STATS,	/* toggle the frame time / FPS line */
	ACT_COUNT
};

enum { KEYS_PER_ACTION = 2, KEY_NONE = -1 };

struct Config {
	int scale;		/* framebuffer 320x200 * scale: 1 or 2 */
	int windowWidth;
	int windowHeight;
	bool fullscreen;	/* borderless on the primary monitor at its current mode */
	bool vsync;
	float fov;		/* horizontal degrees */
	float sfxVolume;	/* 0..1 */
	float musicVolume;	/* 0..1 */
	bool music;
	char soundfont[256];	/* .sf2 for the music, "" = search the usual places */
	int keys[ACT_COUNT][KEYS_PER_ACTION];	/* GLFW key codes or KEY_NONE */
};

void config_defaults(Config *config);

/* Returns false if the file does not exist (config left at its defaults). */
bool config_load(Config *config, const char *path);
bool config_save(const Config *config, const char *path);

#endif
