// Copyright (C) 2026 Lubos Dolezel
//
// Kernels for the texture-gather test: gather() and gather_compare() on the texture classes that have them.
// Each thread gathers at the centre of one texel of the source and stores the four values.

#include <metal_stdlib>
using namespace metal;

constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::nearest);

// The size of the source comes in a buffer: the kernels test gather, not the size queries.
// coordinates for the texel (gid.x, gid.y) of a texture of the given size, shifted by `frac` of a texel
static float2 centre(uint2 gid, uint2 size, float2 frac) {
	return (float2(gid) + frac) / float2(size);
}

kernel void gather_2d_x(texture2d<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)));
}

kernel void gather_2d_y(texture2d<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)), int2(0), component::y);
}

kernel void gather_2d_z(texture2d<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)), int2(0), component::z);
}

kernel void gather_2d_w(texture2d<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)), int2(0), component::w);
}

kernel void gather_2d_offset(texture2d<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)), int2(1, 0), component::y);
}

kernel void gather_2d_uint(texture2d<uint> src [[texture(0)]], device uint4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)), int2(0), component::z);
}

kernel void gather_2d_int(texture2d<int> src [[texture(0)]], device int4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)));
}

kernel void gather_2d_half(texture2d<half> src [[texture(0)]], device half4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)), int2(0), component::y);
}

kernel void gather_2d_array(texture2d_array<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint3 gid [[thread_position_in_grid]]) {
	out[(gid.z * size.y + gid.y) * size.x + gid.x] = src.gather(s, centre(gid.xy, size, float2(0.75, 0.75)), gid.z, int2(0), component::y);
}

kernel void gather_cube(texturecube<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	// the +x face: direction (1, v, -u)
	uint dimension = size.x;
	float2 uv = (float2(gid) + 0.75) / float(dimension) * 2.0 - 1.0;
	out[gid.y * dimension + gid.x] = src.gather(s, float3(1.0, -uv.y, -uv.x), component::y);
}

kernel void gather_depth(depth2d<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)));
}

kernel void gather_depth_offset(depth2d<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather(s, centre(gid, size, float2(0.75, 0.75)), int2(0, 1));
}

kernel void gather_depth_array(depth2d_array<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint2& size [[buffer(2)]], uint3 gid [[thread_position_in_grid]]) {
	out[(gid.z * size.y + gid.y) * size.x + gid.x] = src.gather(s, centre(gid.xy, size, float2(0.75, 0.75)), gid.z);
}

constexpr sampler sc(coord::normalized, address::clamp_to_edge, filter::nearest, compare_func::less);

kernel void gather_compare_depth(depth2d<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant float& ref [[buffer(1)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather_compare(sc, centre(gid, size, float2(0.75, 0.75)), ref);
}

kernel void gather_compare_depth_offset(depth2d<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant float& ref [[buffer(1)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * size.x + gid.x] = src.gather_compare(sc, centre(gid, size, float2(0.75, 0.75)), ref, int2(1, 0));
}

kernel void gather_compare_depth_array(depth2d_array<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant float& ref [[buffer(1)]], constant uint2& size [[buffer(2)]], uint3 gid [[thread_position_in_grid]]) {
	out[(gid.z * size.y + gid.y) * size.x + gid.x] = src.gather_compare(sc, centre(gid.xy, size, float2(0.75, 0.75)), gid.z, ref);
}

kernel void gather_compare_cube(depthcube<float> src [[texture(0)]], device float4* out [[buffer(0)]], constant float& ref [[buffer(1)]], constant uint2& size [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	uint dimension = size.x;
	float2 uv = (float2(gid) + 0.75) / float(dimension) * 2.0 - 1.0;
	out[gid.y * dimension + gid.x] = src.gather_compare(sc, float3(1.0, -uv.y, -uv.x), ref);
}
