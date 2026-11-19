#include <metal_stdlib>
using namespace metal;

struct Vertex {
	float4 position [[position]];
};

// a triangle that covers the whole target, at depth 0.5
vertex Vertex coverVertex(uint vid [[vertex_id]]) {
	Vertex o;
	float x = (vid == 1) ? 3.0 : -1.0;
	float y = (vid == 2) ? 3.0 : -1.0;
	o.position = float4(x, y, 0.5, 1.0);
	return o;
}

fragment float4 redFragment() {
	return float4(1.0, 0.0, 0.0, 1.0);
}

// shows the depth that earlier draws of the pass wrote at this pixel
fragment float4 depthReadFragment(Vertex in [[stage_in]], texture2d<float> depth [[texture(0)]], sampler depthSampler [[sampler(0)]]) {
	return float4(depth.sample(depthSampler, in.position.xy / 16.0).r, 0.0, 0.0, 1.0);
}
