#include "Video.h"

#include "AsmRoutines.h"
#include "GLCheck.h"
#include "Palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_text_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Cannot open %s (run from the repo root)\n", path);
		return nullptr;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char *text = (char *)malloc((size_t)size + 1);
	size_t got = fread(text, 1, (size_t)size, f);
	fclose(f);
	text[got] = '\0';
	return text;
}

static GLuint compile_shader(GLenum type, const char *path)
{
	char *source = read_text_file(path);
	if (!source)
		return 0;

	GLuint shader = glCreateShader(type);
	GL_CHECK(glShaderSource(shader, 1, &source, nullptr));
	GL_CHECK(glCompileShader(shader));
	free(source);

	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		fprintf(stderr, "%s: %s\n", path, log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint link_program(const char *vertPath, const char *fragPath)
{
	GLuint vert = compile_shader(GL_VERTEX_SHADER, vertPath);
	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fragPath);
	if (!vert || !frag) {
		glDeleteShader(vert);
		glDeleteShader(frag);
		return 0;
	}

	GLuint program = glCreateProgram();
	GL_CHECK(glAttachShader(program, vert));
	GL_CHECK(glAttachShader(program, frag));
	GL_CHECK(glLinkProgram(program));
	glDeleteShader(vert);
	glDeleteShader(frag);

	GLint ok = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(program, sizeof(log), nullptr, log);
		fprintf(stderr, "link %s + %s: %s\n", vertPath, fragPath, log);
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

static GLuint make_nearest_texture()
{
	GLuint tex;
	GL_CHECK(glGenTextures(1, &tex));
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, tex));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	return tex;
}

bool video_init(Video *video, int scale)
{
	memset(video, 0, sizeof(*video));
	video->width = VIDEO_BASE_WIDTH * scale;
	video->height = VIDEO_BASE_HEIGHT * scale;
	video->pitch = video->width;
	video->fb = (uint8_t *)calloc((size_t)video->pitch * (size_t)video->height, 1);

	video->program = link_program("shaders/present.vert", "shaders/present.frag");
	if (!video->program) {
		video_shutdown(video);
		return false;
	}

	/* Core profile refuses to draw without a VAO, even with no attributes. */
	GL_CHECK(glGenVertexArrays(1, &video->vao));

	/* Rows of 1-byte pixels are not 4-byte aligned in general. */
	GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));

	video->fbTexture = make_nearest_texture();
	GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, video->width, video->height, 0,
		GL_RED, GL_UNSIGNED_BYTE, video->fb));

	video->palTexture = make_nearest_texture();
	GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, PAL_COLORS, 1, 0,
		GL_RGB, GL_UNSIGNED_BYTE, nullptr));

	GL_CHECK(glUseProgram(video->program));
	GL_CHECK(glUniform1i(glGetUniformLocation(video->program, "u_framebuffer"), 0));
	GL_CHECK(glUniform1i(glGetUniformLocation(video->program, "u_palette"), 1));

	Palette pal;
	palette_build_default(&pal);
	video_set_palette(video, &pal);
	return true;
}

void video_shutdown(Video *video)
{
	if (video->fbTexture)
		glDeleteTextures(1, &video->fbTexture);
	if (video->palTexture)
		glDeleteTextures(1, &video->palTexture);
	if (video->vao)
		glDeleteVertexArrays(1, &video->vao);
	if (video->program)
		glDeleteProgram(video->program);
	free(video->fb);
	memset(video, 0, sizeof(*video));
}

void video_set_palette(Video *video, const Palette *pal)
{
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, video->palTexture));
	GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PAL_COLORS, 1,
		GL_RGB, GL_UNSIGNED_BYTE, pal->rgb));
}

void video_present(Video *video, int windowWidth, int windowHeight)
{
	/* Black bars: clear the whole window, then draw into the 4:3 viewport. */
	GL_CHECK(glViewport(0, 0, windowWidth, windowHeight));
	GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
	GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));

	int vw = windowWidth;
	int vh = windowHeight;
	if ((float)windowWidth > (float)windowHeight * VIDEO_DISPLAY_ASPECT)
		vw = (int)((float)windowHeight * VIDEO_DISPLAY_ASPECT + 0.5f);
	else
		vh = (int)((float)windowWidth / VIDEO_DISPLAY_ASPECT + 0.5f);
	if (vw <= 0 || vh <= 0)
		return;
	GL_CHECK(glViewport((windowWidth - vw) / 2, (windowHeight - vh) / 2, vw, vh));

	GL_CHECK(glActiveTexture(GL_TEXTURE0));
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, video->fbTexture));
	GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, video->width, video->height,
		GL_RED, GL_UNSIGNED_BYTE, video->fb));
	GL_CHECK(glActiveTexture(GL_TEXTURE1));
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, video->palTexture));

	GL_CHECK(glUseProgram(video->program));
	GL_CHECK(glBindVertexArray(video->vao));
	GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, 3));
}

/* ------------------------------------------------------------------------- */
/* Test pattern                                                              */
/* ------------------------------------------------------------------------- */

static void fill_rect(Video *video, int x0, int y0, int w, int h, uint8_t color)
{
	for (int y = y0; y < y0 + h; y++) {
		if (y < 0 || y >= video->height)
			continue;
		for (int x = x0; x < x0 + w; x++) {
			if (x >= 0 && x < video->width)
				video->fb[y * video->pitch + x] = color;
		}
	}
}

void video_draw_test_pattern(Video *video, int frame)
{
	const int s = video->width / VIDEO_BASE_WIDTH;
	const int w = video->width;
	const int h = video->height;
	const uint8_t white = 15;		/* gray ramp, brightest */
	const uint8_t red = 1 * 16 + 15;	/* ramp 1 = hue 0 */
	const uint8_t blue = 11 * 16 + 15;

	RC_fb_clear(video->fb, (uint32_t)(video->pitch * h), 0);

	/* 1-pixel border: every edge must be visible, nothing cropped. */
	fill_rect(video, 0, 0, w, 1, white);
	fill_rect(video, 0, h - 1, w, 1, white);
	fill_rect(video, 0, 0, 1, h, white);
	fill_rect(video, w - 1, 0, 1, h, white);

	/* Top-left marker: catches a flipped image on either axis. */
	fill_rect(video, 3 * s, 3 * s, 8 * s, 8 * s, red);

	/* All 256 palette entries, 16x16 swatches. Bottom-right is index 255. */
	const int cell = 10 * s;
	for (int i = 0; i < PAL_COLORS; i++)
		fill_rect(video, 16 * s + (i % 16) * cell, 16 * s + (i / 16) * cell,
			cell - s, cell - s, (uint8_t)i);

	/*
	 * Circle: 120 pixels wide but only 100 rows tall in the framebuffer,
	 * because each pixel is shown 1.2x taller than wide. Round on screen
	 * only if the 4:3 letterbox is right.
	 */
	const float cx = 250.0f * (float)s;
	const float cy = 96.0f * (float)s;
	const float rx = 60.0f * (float)s;
	const float ry = rx / 1.2f;
	for (int y = (int)(cy - ry) - 1; y <= (int)(cy + ry) + 1; y++) {
		for (int x = (int)(cx - rx) - 1; x <= (int)(cx + rx) + 1; x++) {
			float dx = ((float)x + 0.5f - cx) / rx;
			float dy = ((float)y + 0.5f - cy) / ry;
			float d = dx * dx + dy * dy;
			if (d <= 1.0f && d >= 0.88f)
				fill_rect(video, x, y, 1, 1, white);
		}
	}
	/* Square inside the circle: 60x50 pixels, should look square. */
	fill_rect(video, (int)(cx - 30.0f * (float)s), (int)(cy - 25.0f * (float)s),
		60 * s, 1, white);
	fill_rect(video, (int)(cx - 30.0f * (float)s), (int)(cy + 25.0f * (float)s) - 1,
		60 * s, 1, white);
	fill_rect(video, (int)(cx - 30.0f * (float)s), (int)(cy - 25.0f * (float)s),
		1, 50 * s, white);
	fill_rect(video, (int)(cx + 30.0f * (float)s) - 1, (int)(cy - 25.0f * (float)s),
		1, 50 * s, white);

	/* Moving bar: proves the framebuffer is re-uploaded every frame. */
	const int barW = 16 * s;
	const int barX = (frame * s) % (w - barW - 2 * s) + s;
	fill_rect(video, barX, 184 * s, barW, 10 * s, blue);
}
