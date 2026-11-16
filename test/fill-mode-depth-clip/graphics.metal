#include <metal_stdlib>
using namespace metal;

struct Vertex {
	float4 position [[position]];
};

// a triangle inside the target, with room around it
vertex Vertex smallVertex(uint vid [[vertex_id]]) {
	Vertex o;
	float x = (vid == 0) ? -0.75 : ((vid == 1) ? 0.75 : 0.0);
	float y = (vid == 2) ? 0.75 : -0.75;
	o.position = float4(x, y, 0.5, 1.0);
	return o;
}

// a triangle that covers the whole target but lies beyond the far plane (clip z is greater than w)
vertex Vertex farVertex(uint vid [[vertex_id]]) {
	Vertex o;
	float x = (vid == 1) ? 3.0 : -1.0;
	float y = (vid == 2) ? 3.0 : -1.0;
	o.position = float4(x, y, 2.0, 1.0);
	return o;
}

fragment float4 redFragment() {
	return float4(1.0, 0.0, 0.0, 1.0);
}
