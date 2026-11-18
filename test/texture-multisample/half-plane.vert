#version 450
// Copyright (C) 2026 Lubos Dolezel
// covers everything left of x = -0.25 (an edge through the middle of the second pixel of a four pixel wide target)
void main() {
    const vec2 vertices[3] = vec2[](vec2(-0.25, -1), vec2(-0.25, 3), vec2(-4.25, -1));
    gl_Position = vec4(vertices[gl_VertexIndex], 0, 1);
}
