#version 330 core

// Fullscreen triangle from gl_VertexID, no vertex buffer.
// Vertices (-1,-1) (3,-1) (-1,3) cover the viewport; the overhang is clipped.

out vec2 v_uv;

void main()
{
	vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	// Framebuffer row 0 is the top of the screen; GL textures start at the bottom.
	v_uv = vec2(pos.x, 1.0 - pos.y);
	gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
