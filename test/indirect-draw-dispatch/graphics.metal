#include <metal_stdlib>
using namespace metal;

struct Vertex {
	float4 position [[position]];
};

// a triangle that covers the whole target for vertex ids 0, 1, 2 and is empty for any other
vertex Vertex triangleVertex(uint vid [[vertex_id]]) {
	Vertex o;
	float x = (vid == 1) ? 3.0 : -1.0;
	float y = (vid == 2) ? 3.0 : -1.0;
	o.position = (vid < 3) ? float4(x, y, 0.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
	return o;
}

fragment float4 redFragment() {
	return float4(1.0, 0.0, 0.0, 1.0);
}
