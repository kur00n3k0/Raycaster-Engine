#ifndef WINDOW_H
#define WINDOW_H

struct GLFWwindow;

/*
 * Owns the GLFW window and its OpenGL 3.3 core context.
 * No exceptions: call open() and check the result.
 */
class Window {
	public:
		Window();
		~Window();

		/* fullscreen: borderless at the primary monitor's current video mode. */
		bool open(const char *title, int width, int height, bool fullscreen, bool vsync);
		void close();

		bool shouldClose() const;
		void requestClose();
		void pollEvents();
		void swapBuffers();

		bool keyDown(int key) const;
		void framebufferSize(int *width, int *height) const;
		double time() const;

	private:
		Window(const Window &) = delete;
		Window &operator=(const Window &) = delete;

		GLFWwindow *m_window;
		bool m_glfwInit;
};

#endif
