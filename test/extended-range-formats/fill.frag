#version 450
#extension GL_GOOGLE_include_directive : require
#include "fill-values.glsl"
layout(location=0) out vec4 color;
void main() {
	float v = fillValues[int(gl_FragCoord.x)];
	color = vec4(v, v, v, v);
}
