#include "Window.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <stdio.h>

static void glfw_error(int code, const char *msg)
{
	fprintf(stderr, "GLFW error %d: %s\n", code, msg);
}

Window::Window() : m_window(nullptr), m_glfwInit(false) {}

Window::~Window()
{
	close();
}

bool Window::open(const char *title, int width, int height, bool fullscreen, bool vsync)
{
	glfwSetErrorCallback(glfw_error);
	if (!glfwInit())
		return false;
	m_glfwInit = true;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#if RC_DEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

	GLFWmonitor *monitor = nullptr;
	if (fullscreen) {
		monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode *mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
		if (mode) {
			glfwWindowHint(GLFW_RED_BITS, mode->redBits);
			glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
			glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
			glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
			width = mode->width;
			height = mode->height;
		} else {
			monitor = nullptr;
		}
	}

	m_window = glfwCreateWindow(width, height, title, monitor, nullptr);
	if (!m_window) {
		close();
		return false;
	}
	glfwMakeContextCurrent(m_window);
	glfwSwapInterval(vsync ? 1 : 0);

	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK) {
		fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(err));
		close();
		return false;
	}
	/* glewInit on a core context leaves a harmless GL_INVALID_ENUM behind. */
	while (glGetError() != GL_NO_ERROR) {}

	printf("GL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
	return true;
}

void Window::close()
{
	if (m_window) {
		glfwDestroyWindow(m_window);
		m_window = nullptr;
	}
	if (m_glfwInit) {
		glfwTerminate();
		m_glfwInit = false;
	}
}

bool Window::shouldClose() const
{
	return glfwWindowShouldClose(m_window);
}

void Window::requestClose()
{
	glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::pollEvents()
{
	glfwPollEvents();
}

void Window::swapBuffers()
{
	glfwSwapBuffers(m_window);
}

bool Window::keyDown(int key) const
{
	return glfwGetKey(m_window, key) == GLFW_PRESS;
}

void Window::framebufferSize(int *width, int *height) const
{
	glfwGetFramebufferSize(m_window, width, height);
}

double Window::time() const
{
	return glfwGetTime();
}
