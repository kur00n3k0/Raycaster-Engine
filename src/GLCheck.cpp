#include "GLCheck.h"

#include <stdio.h>

#if RC_DEBUG

static const char *gl_error_name(GLenum err)
{
	switch (err) {
	case GL_INVALID_ENUM:                  return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:                 return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:             return "GL_INVALID_OPERATION";
	case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
	case GL_OUT_OF_MEMORY:                 return "GL_OUT_OF_MEMORY";
	default:                               return "unknown GL error";
	}
}

void gl_check_error(const char *call, const char *file, int line)
{
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR)
		fprintf(stderr, "%s:%d: %s -> %s\n", file, line, call, gl_error_name(err));
}

#endif
