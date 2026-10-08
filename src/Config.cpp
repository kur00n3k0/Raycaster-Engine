#include "Config.h"

#include <GLFW/glfw3.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ------------------------------------------------------------------------- */
/* Key names                                                                 */
/* ------------------------------------------------------------------------- */

struct KeyName {
	const char *name;
	int key;
};

/* Named keys; letters, digits and F1-F12 are handled in key_from_name. */
static const KeyName KEY_NAMES[] = {
	{ "SPACE", GLFW_KEY_SPACE }, { "COMMA", GLFW_KEY_COMMA }, { "PERIOD", GLFW_KEY_PERIOD },
	{ "SLASH", GLFW_KEY_SLASH }, { "SEMICOLON", GLFW_KEY_SEMICOLON },
	{ "APOSTROPHE", GLFW_KEY_APOSTROPHE }, { "MINUS", GLFW_KEY_MINUS }, { "EQUAL", GLFW_KEY_EQUAL },
	{ "LBRACKET", GLFW_KEY_LEFT_BRACKET }, { "RBRACKET", GLFW_KEY_RIGHT_BRACKET },
	{ "BACKSLASH", GLFW_KEY_BACKSLASH }, { "GRAVE", GLFW_KEY_GRAVE_ACCENT },
	{ "ENTER", GLFW_KEY_ENTER }, { "TAB", GLFW_KEY_TAB }, { "BACKSPACE", GLFW_KEY_BACKSPACE },
	{ "INSERT", GLFW_KEY_INSERT }, { "DELETE", GLFW_KEY_DELETE }, { "HOME", GLFW_KEY_HOME },
	{ "END", GLFW_KEY_END }, { "PAGEUP", GLFW_KEY_PAGE_UP }, { "PAGEDOWN", GLFW_KEY_PAGE_DOWN },
	{ "UP", GLFW_KEY_UP }, { "DOWN", GLFW_KEY_DOWN }, { "LEFT", GLFW_KEY_LEFT }, { "RIGHT", GLFW_KEY_RIGHT },
	{ "LSHIFT", GLFW_KEY_LEFT_SHIFT }, { "RSHIFT", GLFW_KEY_RIGHT_SHIFT },
	{ "LCTRL", GLFW_KEY_LEFT_CONTROL }, { "RCTRL", GLFW_KEY_RIGHT_CONTROL },
	{ "LALT", GLFW_KEY_LEFT_ALT }, { "RALT", GLFW_KEY_RIGHT_ALT },
};

static int key_from_name(const char *name)
{
	size_t len = strlen(name);
	if (len == 1 && isalpha((unsigned char)name[0]))
		return GLFW_KEY_A + (toupper((unsigned char)name[0]) - 'A');
	if (len == 1 && isdigit((unsigned char)name[0]))
		return GLFW_KEY_0 + (name[0] - '0');
	if ((name[0] == 'F' || name[0] == 'f') && len >= 2 && len <= 3) {
		int n = atoi(name + 1);
		if (n >= 1 && n <= 12)
			return GLFW_KEY_F1 + n - 1;
	}
	if (strncasecmp(name, "KP", 2) == 0 && len == 3 && isdigit((unsigned char)name[2]))
		return GLFW_KEY_KP_0 + (name[2] - '0');
	for (const KeyName &k : KEY_NAMES) {
		if (strcasecmp(k.name, name) == 0)
			return k.key;
	}
	return KEY_NONE;
}

/* Writes the canonical name of `key` into buf (for saving). */
static void name_from_key(int key, char *buf, size_t size)
{
	if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
		snprintf(buf, size, "%c", 'A' + (key - GLFW_KEY_A));
	else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
		snprintf(buf, size, "%c", '0' + (key - GLFW_KEY_0));
	else if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
		snprintf(buf, size, "F%d", key - GLFW_KEY_F1 + 1);
	else if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
		snprintf(buf, size, "KP%d", key - GLFW_KEY_KP_0);
	else {
		snprintf(buf, size, "?");
		for (const KeyName &k : KEY_NAMES) {
			if (k.key == key) {
				snprintf(buf, size, "%s", k.name);
				break;
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Settings table                                                            */
/* ------------------------------------------------------------------------- */

static const char *const ACTION_NAMES[ACT_COUNT] = {
	"key_forward", "key_back", "key_strafe_left", "key_strafe_right",
	"key_turn_left", "key_turn_right", "key_run", "key_use", "key_fire",
	"key_weapon_fists", "key_weapon_pistol", "key_weapon_smg", "key_stats"
};

void config_defaults(Config *c)
{
	c->scale = 1;
	c->windowWidth = 960;
	c->windowHeight = 720;
	c->fullscreen = false;
	c->vsync = true;
	c->fov = 70.0f;
	c->sfxVolume = 1.0f;
	c->musicVolume = 1.0f;
	c->music = true;
	c->soundfont[0] = '\0';

	static const int DEFAULT_KEYS[ACT_COUNT][KEYS_PER_ACTION] = {
		{ GLFW_KEY_W, GLFW_KEY_UP },
		{ GLFW_KEY_S, GLFW_KEY_DOWN },
		{ GLFW_KEY_A, KEY_NONE },
		{ GLFW_KEY_D, KEY_NONE },
		{ GLFW_KEY_COMMA, KEY_NONE },
		{ GLFW_KEY_PERIOD, KEY_NONE },
		{ GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT },
		{ GLFW_KEY_SPACE, KEY_NONE },
		{ GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL },
		{ GLFW_KEY_1, KEY_NONE },
		{ GLFW_KEY_2, KEY_NONE },
		{ GLFW_KEY_3, KEY_NONE },
		{ GLFW_KEY_F1, KEY_NONE },
	};
	memcpy(c->keys, DEFAULT_KEYS, sizeof(c->keys));
}

static bool parse_bool(const char *v, bool *out)
{
	if (!strcasecmp(v, "1") || !strcasecmp(v, "yes") || !strcasecmp(v, "true") || !strcasecmp(v, "on")) {
		*out = true;
		return true;
	}
	if (!strcasecmp(v, "0") || !strcasecmp(v, "no") || !strcasecmp(v, "false") || !strcasecmp(v, "off")) {
		*out = false;
		return true;
	}
	return false;
}

static bool parse_int(const char *v, int lo, int hi, int *out)
{
	char *end;
	long n = strtol(v, &end, 10);
	if (*end || end == v || n < lo || n > hi)
		return false;
	*out = (int)n;
	return true;
}

static bool parse_float(const char *v, float lo, float hi, float *out)
{
	char *end;
	float f = strtof(v, &end);
	if (*end || end == v || f < lo || f > hi)
		return false;
	*out = f;
	return true;
}

/* "W UP" -> two key codes. Unknown names fail the whole line. */
static bool parse_keys(char *v, int out[KEYS_PER_ACTION])
{
	int keys[KEYS_PER_ACTION] = { KEY_NONE, KEY_NONE };
	int n = 0;
	for (char *tok = strtok(v, " \t"); tok; tok = strtok(nullptr, " \t")) {
		if (n == KEYS_PER_ACTION)
			return false;
		int key = key_from_name(tok);
		if (key == KEY_NONE)
			return false;
		keys[n++] = key;
	}
	if (n == 0)
		return false;
	memcpy(out, keys, sizeof(keys));
	return true;
}

/* Any text that fits; empty is allowed. Paths can't contain '#' (it starts a comment). */
static bool parse_string(const char *v, char *out, size_t size)
{
	if (strlen(v) >= size)
		return false;
	strcpy(out, v);
	return true;
}

static char *trim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	char *end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return s;
}

/* Apply one setting; false if the name is unknown or the value is invalid. */
static bool apply(Config *c, const char *name, char *value)
{
	if (!strcmp(name, "scale"))		return parse_int(value, 1, 2, &c->scale);
	if (!strcmp(name, "window_width"))	return parse_int(value, 320, 16384, &c->windowWidth);
	if (!strcmp(name, "window_height"))	return parse_int(value, 200, 16384, &c->windowHeight);
	if (!strcmp(name, "fullscreen"))	return parse_bool(value, &c->fullscreen);
	if (!strcmp(name, "vsync"))		return parse_bool(value, &c->vsync);
	if (!strcmp(name, "fov"))		return parse_float(value, 50.0f, 110.0f, &c->fov);
	if (!strcmp(name, "sfx_volume"))	return parse_float(value, 0.0f, 1.0f, &c->sfxVolume);
	if (!strcmp(name, "music_volume"))	return parse_float(value, 0.0f, 1.0f, &c->musicVolume);
	if (!strcmp(name, "music"))		return parse_bool(value, &c->music);
	if (!strcmp(name, "soundfont"))	return parse_string(value, c->soundfont, sizeof(c->soundfont));
	for (int a = 0; a < ACT_COUNT; a++) {
		if (!strcmp(name, ACTION_NAMES[a]))
			return parse_keys(value, c->keys[a]);
	}
	return false;
}

bool config_load(Config *config, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return false;

	char line[512];
	int lineNo = 0;
	while (fgets(line, sizeof(line), f)) {
		lineNo++;
		char *hash = strchr(line, '#');
		if (hash)
			*hash = '\0';
		char *s = trim(line);
		if (!*s)
			continue;
		char *eq = strchr(s, '=');
		if (!eq) {
			fprintf(stderr, "%s:%d: expected name = value\n", path, lineNo);
			continue;
		}
		*eq = '\0';
		char *name = trim(s);
		char *value = trim(eq + 1);
		if (!apply(config, name, value))
			fprintf(stderr, "%s:%d: ignoring '%s': unknown setting or bad value\n", path, lineNo, name);
	}
	fclose(f);
	return true;
}

bool config_save(const Config *c, const char *path)
{
	FILE *f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "Cannot write %s\n", path);
		return false;
	}
	fprintf(f,
		"# Raycaster settings. One \"name = value\" per line, '#' starts a comment.\n"
		"# Delete this file to get the defaults back.\n\n");

	/* "name = value" padded so the comments line up. */
	char line[300];
	snprintf(line, sizeof(line), "scale = %d", c->scale);
	fprintf(f, "%-24s# framebuffer 320x200 (1) or 640x400 (2)\n", line);
	snprintf(line, sizeof(line), "window_width = %d", c->windowWidth);
	fprintf(f, "%-24s# ignored when fullscreen\n", line);
	snprintf(line, sizeof(line), "window_height = %d", c->windowHeight);
	fprintf(f, "%-24s# ignored when fullscreen\n", line);
	snprintf(line, sizeof(line), "fullscreen = %s", c->fullscreen ? "yes" : "no");
	fprintf(f, "%-24s# borderless, at the desktop resolution\n", line);
	snprintf(line, sizeof(line), "vsync = %s", c->vsync ? "yes" : "no");
	fprintf(f, "%-24s# frame rate is capped at 60 either way\n", line);
	snprintf(line, sizeof(line), "fov = %g", (double)c->fov);
	fprintf(f, "%-24s# horizontal field of view, 50 - 110 degrees\n", line);
	snprintf(line, sizeof(line), "sfx_volume = %g", (double)c->sfxVolume);
	fprintf(f, "%-24s# 0 - 1\n", line);
	snprintf(line, sizeof(line), "music_volume = %g", (double)c->musicVolume);
	fprintf(f, "%-24s# 0 - 1\n", line);
	snprintf(line, sizeof(line), "music = %s", c->music ? "yes" : "no");
	fprintf(f, "%-24s# MIDI music on or off\n", line);
	snprintf(line, sizeof(line), "soundfont = %s", c->soundfont);
	fprintf(f, "%-24s# General MIDI .sf2 for the music; empty = search the usual places\n\n", line);

	fprintf(f,
		"# Keys: up to two per action. Names: A-Z, 0-9, F1-F12, KP0-KP9, SPACE, COMMA, PERIOD,\n"
		"# SLASH, SEMICOLON, APOSTROPHE, MINUS, EQUAL, LBRACKET, RBRACKET, BACKSLASH, GRAVE,\n"
		"# ENTER, TAB, BACKSPACE, INSERT, DELETE, HOME, END, PAGEUP, PAGEDOWN,\n"
		"# UP, DOWN, LEFT, RIGHT, LSHIFT, RSHIFT, LCTRL, RCTRL, LALT, RALT. Esc always quits.\n");
	for (int a = 0; a < ACT_COUNT; a++) {
		fprintf(f, "%s =", ACTION_NAMES[a]);
		for (int k = 0; k < KEYS_PER_ACTION; k++) {
			if (c->keys[a][k] == KEY_NONE)
				continue;
			char name[16];
			name_from_key(c->keys[a][k], name, sizeof(name));
			fprintf(f, " %s", name);
		}
		fputc('\n', f);
	}
	fclose(f);
	return true;
}
