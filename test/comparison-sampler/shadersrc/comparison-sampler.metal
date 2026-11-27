#include <metal_stdlib>
using namespace metal;

// Reads the colour texture through the sampler with an ordinary sample: the sampler's compare
// function must play no part.
kernel void sample_color(texture2d<float> color [[texture(0)]], sampler s [[sampler(0)]],
		device float4* out [[buffer(0)]]) {
	out[0] = color.sample(s, float2(0.5), level(0));
}

// Compares two references against the depth texture through the same kind of sampler.
kernel void sample_depth_compare(depth2d<float> depth [[texture(0)]], sampler s [[sampler(0)]],
		device float* out [[buffer(0)]]) {
	out[0] = depth.sample_compare(s, float2(0.5), 0.25, level(0));
	out[1] = depth.sample_compare(s, float2(0.5), 0.75, level(0));
}

// A comparison in a helper that takes the sampler as a parameter, next to an ordinary sample through a second
// sampler, and a gather_compare: only the sampler reaching the comparisons may compare.
static float shadow(depth2d<float> depth, sampler s, float reference) {
	return depth.sample_compare(s, float2(0.5), reference, level(0));
}

kernel void helper_compare(texture2d<float> color [[texture(0)]], depth2d<float> depth [[texture(1)]],
		sampler plain [[sampler(0)]], sampler compare [[sampler(1)]], device float* out [[buffer(0)]]) {
	float4 texel = color.sample(plain, float2(0.5), level(0));
	out[0] = texel.x;
	out[1] = texel.y;
	out[2] = texel.z;
	out[3] = texel.w;
	out[4] = shadow(depth, compare, 0.25);
	out[5] = shadow(depth, compare, 0.75);
	out[6] = depth.gather_compare(compare, float2(0.5), 0.25).x;
}
