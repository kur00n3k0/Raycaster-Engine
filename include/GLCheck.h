#ifndef GLCHECK_H
#define GLCHECK_H

#include <GL/glew.h>

/*
 * GL_CHECK(glFoo(...)) runs the call and, in Debug builds, reports any GL
 * error with the call text and source location. Release builds compile it
 * down to the bare call.
 */

#if RC_DEBUG
	void gl_check_error(const char *call, const char *file, int line);
	#define GL_CHECK(call) do { call; gl_check_error(#call, __FILE__, __LINE__); } while (0)
#else
	#define GL_CHECK(call) do { call; } while (0)
#endif

#endif
