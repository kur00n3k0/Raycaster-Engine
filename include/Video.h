#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

/*
 * The "video card": an 8-bit indexed framebuffer in system memory that is
 * uploaded as a GL_R8 texture each frame and drawn as one fullscreen
 * triangle. The fragment shader maps each index through a 256x1 palette.
 *
 * The framebuffer is row-major: pixel (x, y) is fb[y * pitch + x].
 * It is shown at 4:3 regardless of its pixel size (VGA non-square pixels).
 */

enum {
	VIDEO_BASE_WIDTH = 320,
	VIDEO_BASE_HEIGHT = 200
};

/* Displayed aspect ratio of the framebuffer, like a 320x200 VGA mode on a 4:3 CRT. */
static const float VIDEO_DISPLAY_ASPECT = 4.0f / 3.0f;

struct Palette;

struct Video {
	int width;
	int height;
	int pitch;
	uint8_t *fb;

	unsigned program;
	unsigned vao;
	unsigned fbTexture;
	unsigned palTexture;
};

/* scale 1 = 320x200, 2 = 640x400. Needs a current GL context. */
bool video_init(Video *video, int scale);
void video_shutdown(Video *video);

void video_set_palette(Video *video, const Palette *pal);

/* Upload the framebuffer and draw it letterboxed to 4:3 in the window. */
void video_present(Video *video, int windowWidth, int windowHeight);

/* Debug: swatches, a circle that is only round at 4:3, border, moving bar. */
void video_draw_test_pattern(Video *video, int frame);

#endif
