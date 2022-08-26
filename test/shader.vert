#version 450

//#extension GL_EXT_debug_printf : enable

struct Vertex {
	vec2 position;
	vec4 color;
};

layout(std430, binding = 0) readonly buffer VertexBlock {
	Vertex vertices[];
};
layout(std430, binding = 1) readonly buffer ViewportBlock {
	uvec2 viewport_size;
};

layout(location = 0) out vec4 out_color;

void main() {
	vec2 viewport_size_float = viewport_size;

	//debugPrintfEXT("position = (%f, %f)", vertices[gl_VertexIndex].position.x, vertices[gl_VertexIndex].position.y);

	gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
	gl_Position.xy = vertices[gl_VertexIndex].position / (viewport_size_float / 2.0);

	out_color = vertices[gl_VertexIndex].color;
}
