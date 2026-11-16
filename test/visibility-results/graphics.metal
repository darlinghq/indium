// Copyright (C) 2026 Lubos Dolezel
#include <metal_stdlib>
using namespace metal;

struct Vertex {
	float4 position [[position]];
};

// a rectangle in normalized device coordinates, at the depth z (two triangles, vertex_id 0 to 5)
struct Quad {
	float x0;
	float y0;
	float x1;
	float y1;
	float z;
};

vertex Vertex quadVertex(uint vid [[vertex_id]], constant Quad& q [[buffer(0)]]) {
	Vertex o;
	float x = (vid == 1 || vid == 3 || vid == 4) ? q.x1 : q.x0;
	float y = (vid == 2 || vid == 4 || vid == 5) ? q.y1 : q.y0;
	o.position = float4(x, y, q.z, 1.0);
	return o;
}

fragment float4 redFragment() {
	return float4(1.0, 0.0, 0.0, 1.0);
}

// discards the left four pixels of a 16 pixel wide target
fragment float4 discardFragment(Vertex in [[stage_in]]) {
	if (in.position.x < 4.0) {
		discard_fragment();
	}
	return float4(0.0, 1.0, 0.0, 1.0);
}
