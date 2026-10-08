#version 330 core

// Indexed framebuffer -> palette lookup.

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_framebuffer;	// GL_R8, nearest
uniform sampler2D u_palette;		// 256x1 RGB

void main()
{
	int index = int(texture(u_framebuffer, v_uv).r * 255.0 + 0.5);
	o_color = vec4(texelFetch(u_palette, ivec2(index, 0), 0).rgb, 1.0);
}
