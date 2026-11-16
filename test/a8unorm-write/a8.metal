#include <metal_stdlib>
using namespace metal;

struct Vertex {
	float4 position [[position]];
};

// a triangle that covers the whole target
vertex Vertex fullVertex(uint vid [[vertex_id]]) {
	Vertex o;
	float x = (vid == 1) ? 3.0 : -1.0;
	float y = (vid == 2) ? 3.0 : -1.0;
	o.position = float4(x, y, 0.5, 1.0);
	return o;
}

// distinct values in every channel, so a channel that lands in the wrong place is visible
fragment float4 colorFragment() {
	return float4(0.9, 0.8, 0.7, 0.5);
}

kernel void writeTexel(texture2d<float, access::write> tex [[texture(0)]], uint2 gid [[thread_position_in_grid]]) {
	tex.write(float4(0.9, 0.8, 0.7, float(gid.x + 1) / 8.0), gid);
}
