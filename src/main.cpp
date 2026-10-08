#include "Audio.h"
#include "Config.h"
#include "Game.h"
#include "Hud.h"
#include "Palette.h"
#include "Raycaster.h"
#include "Sprites.h"
#include "Textures.h"
#include "Video.h"
#include "Window.h"

#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *START_MAP = "assets/maps/e1m1.txt";
static const char *DEFAULT_CONFIG = "raycaster.cfg";
static const double MAX_FRAME_TIME = 0.25;	/* don't spiral after a hitch */
static const double FRAME_RATE = 60.0;		/* hard cap, with or without vsync */
static const double SPIN_TIME = 0.0005;		/* finish the wait by spinning: sleep wakes late */
static const double STATS_INTERVAL = 0.5;	/* seconds between stats line updates */

#if RC_USE_ASM
static const char *RENDER_PATH = "ASM";
#else
static const char *RENDER_PATH = "C++";
#endif

static bool action_down(const Window *window, const Config *config, int action)
{
	for (int k = 0; k < KEYS_PER_ACTION; k++) {
		int key = config->keys[action][k];
		if (key != KEY_NONE && window->keyDown(key))
			return true;
	}
	return false;
}

static void read_input(const Window *window, const Config *config, Input *input)
{
	input->forward     = action_down(window, config, ACT_FORWARD);
	input->back        = action_down(window, config, ACT_BACK);
	input->strafeLeft  = action_down(window, config, ACT_STRAFE_LEFT);
	input->strafeRight = action_down(window, config, ACT_STRAFE_RIGHT);
	input->turnLeft    = action_down(window, config, ACT_TURN_LEFT);
	input->turnRight   = action_down(window, config, ACT_TURN_RIGHT);
	input->run         = action_down(window, config, ACT_RUN);
	input->use         = action_down(window, config, ACT_USE);
	input->fire        = action_down(window, config, ACT_FIRE);
}

/*
 * Frame limiter: wait until the next 1/FRAME_RATE slot. Sleeps for most of
 * the wait and spins the last half millisecond so frames stay evenly paced.
 * If a frame ran late, the schedule restarts from now instead of rushing.
 */
static void wait_for_next_frame(const Window *window, double *nextFrame)
{
	const double period = 1.0 / FRAME_RATE;
	*nextFrame += period;
	double now = window->time();
	if (now > *nextFrame) {
		*nextFrame = now;
		return;
	}
	double sleepFor = *nextFrame - now - SPIN_TIME;
	if (sleepFor > 0.0) {
		timespec ts;
		ts.tv_sec = (time_t)sleepFor;
		ts.tv_nsec = (long)((sleepFor - (double)ts.tv_sec) * 1e9);
		nanosleep(&ts, nullptr);
	}
	while (window->time() < *nextFrame) {
	}
}

/* Start the song the map asks for (@music), or stop the music for "none". */
static void play_map_music(Audio *audio, const Config *config, const Map *map)
{
	audio_stop_music(audio);
	if (!config->music || !map->music[0])
		return;
	char musicPath[256];
	snprintf(musicPath, sizeof(musicPath), "%s%s", MAP_MUSIC_DIR, map->music);
	if (audio_play_music(audio, musicPath, config->soundfont, AUDIO_MUSIC_GAIN * config->musicVolume))
		printf("Music: %s\n", musicPath);
	else
		fprintf(stderr, "Music disabled\n");
}

static void usage(const char *argv0)
{
	fprintf(stderr, "usage: %s [-config <path>] [-map <path>] [-warp <x> <y> <degrees>] [-nomusic]\n", argv0);
}

int main(int argc, char **argv)
{
	/*
	 * Options:
	 *   -config <path>          settings file (default raycaster.cfg, written if missing)
	 *   -map <path>             load another map
	 *   -warp <x> <y> <degrees> override the player start
	 *   -nomusic                sound effects only
	 */
	const char *configPath = DEFAULT_CONFIG;
	const char *mapPath = START_MAP;
	bool noMusic = false;
	bool warp = false;
	float warpX = 0.0f, warpY = 0.0f, warpDeg = 0.0f;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-nomusic") == 0) {
			noMusic = true;
		} else if (strcmp(argv[i], "-config") == 0 && i + 1 < argc) {
			configPath = argv[++i];
		} else if (strcmp(argv[i], "-map") == 0 && i + 1 < argc) {
			mapPath = argv[++i];
		} else if (strcmp(argv[i], "-warp") == 0 && i + 3 < argc) {
			warp = true;
			warpX = strtof(argv[++i], nullptr);
			warpY = strtof(argv[++i], nullptr);
			warpDeg = strtof(argv[++i], nullptr);
		} else {
			usage(argv[0]);
			return 1;
		}
	}

	static Config config;
	config_defaults(&config);
	if (!config_load(&config, configPath)) {
		printf("No %s, writing one with the defaults\n", configPath);
		config_save(&config, configPath);
	}
	if (noMusic)
		config.music = false;

	Window window;
	if (!window.open("Raycaster", config.windowWidth, config.windowHeight, config.fullscreen, config.vsync)) {
		fprintf(stderr, "Could not open a window with an OpenGL 3.3 core context\n");
		return 1;
	}

	Video video;
	if (!video_init(&video, config.scale)) {
		fprintf(stderr, "Could not initialise video\n");
		return 1;
	}

	static Palette palette;
	static Palette flashes[PAL_FLASH_COUNT];
	static Colormaps colormaps;
	palette_build_default(&palette);
	palette_build_colormaps(&palette, &colormaps);
	palette_build_flashes(&palette, flashes);
	video_set_palette(&video, &flashes[0]);
	int shownPalette = 0;

	static WallTextures walls;
	static FlatTextures flats;
	static SpriteTextures sprites;
	if (!textures_load_walls(&walls, &palette) || !textures_load_flats(&flats, &palette)
		|| !textures_load_sprites(&sprites, &palette))
		return 1;

	RenderAssets assets;
	assets.walls = &walls;
	assets.flats = &flats;
	assets.sprites = &sprites;
	assets.colormaps = &colormaps;

	Raycaster rc;
	const int viewHeight = video.height - HUD_BAR_HEIGHT * config.scale;
	if (!raycaster_init(&rc, &video, config.fov, viewHeight)) {
		fprintf(stderr, "Could not initialise raycaster\n");
		return 1;
	}

	static Audio audio;
	if (!audio_init(&audio, config.sfxVolume))
		return 1;

	static Game game;
	if (!game_init(&game, mapPath))
		return 1;

	/* Each map picks its song (@music in the map file). */
	play_map_music(&audio, &config, &game.map);
	if (warp) {
		game.player.pos = glm::vec2(warpX, warpY);
		game.player.angle = warpDeg * 3.14159265f / 180.0f;
	}

	const double tick = 1.0 / GAME_TICK_RATE;
	double previous = window.time();
	double nextFrame = previous;
	double accumulator = 0.0;

	/* Render timing: CPU time to draw the frame into the framebuffer (raycast + sprites + HUD). */
	bool showStats = false;
	bool statsHeld = false;
	char stats[48] = "";
	double statsRender = 0.0, statsStart = previous;
	int statsFrames = 0;
	double totalRender = 0.0;
	long totalFrames = 0;

	while (!window.shouldClose()) {
		window.pollEvents();
		if (window.keyDown(GLFW_KEY_ESCAPE))
			window.requestClose();

		bool statsDown = action_down(&window, &config, ACT_STATS);
		if (statsDown && !statsHeld)
			showStats = !showStats;
		statsHeld = statsDown;

		double now = window.time();
		double frameTime = now - previous;
		previous = now;
		if (frameTime > MAX_FRAME_TIME)
			frameTime = MAX_FRAME_TIME;
		accumulator += frameTime;

		Input input;
		read_input(&window, &config, &input);
		while (accumulator >= tick) {
			game_tick(&game, &input);
			accumulator -= tick;
		}
		if (game.levelChanged) {
			game.levelChanged = false;
			printf("Level: %s\n", game.mapPath);
			play_map_music(&audio, &config, &game.map);
		}

		audio_set_listener(&audio, game.player.pos, game.player.angle);
		audio_update(&audio);
		for (int i = 0; i < game.soundCount; i++)
			audio_play_at(&audio, game.sounds[i].sfx, game.sounds[i].pos);
		game.soundCount = 0;

		/* Damage and pickup flashes are a palette swap; upload only on change. */
		int flash = game_flash_palette(&game);
		if (flash != shownPalette) {
			video_set_palette(&video, &flashes[flash]);
			shownPalette = flash;
		}

		double renderStart = window.time();
		raycaster_render(&rc, &video, &game.map, &assets, game.player.pos, game.player.angle);
		sprites_render(&rc, &video, &assets, game.entities, game.entityCount,
			game.player.pos, game.player.angle);
		hud_draw(&video, &rc, &game, &assets, showStats ? stats : nullptr);
		double renderTime = window.time() - renderStart;

		statsRender += renderTime;
		statsFrames++;
		totalRender += renderTime;
		totalFrames++;
		if (now - statsStart >= STATS_INTERVAL) {
			snprintf(stats, sizeof(stats), "%s %.2f MS %d FPS", RENDER_PATH,
				statsRender / statsFrames * 1000.0, (int)(statsFrames / (now - statsStart) + 0.5));
			statsRender = 0.0;
			statsFrames = 0;
			statsStart = now;
		}

		int w, h;
		window.framebufferSize(&w, &h);
		video_present(&video, w, h);
		window.swapBuffers();
		wait_for_next_frame(&window, &nextFrame);
	}

	if (totalFrames > 0)
		printf("%s render path: %ld frames, average %.3f ms to draw a %dx%d frame\n",
			RENDER_PATH, totalFrames, totalRender / (double)totalFrames * 1000.0, video.width, video.height);

	game_shutdown(&game);
	audio_shutdown(&audio);
	raycaster_shutdown(&rc);
	textures_free_sprites(&sprites);
	textures_free_flats(&flats);
	textures_free_walls(&walls);
	video_shutdown(&video);
	return 0;
}
