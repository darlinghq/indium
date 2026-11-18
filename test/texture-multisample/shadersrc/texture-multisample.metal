// Copyright (C) 2026 Lubos Dolezel
//
// Kernels for the texture-multisample test: read() of every sample of multisample textures, and the size and
// sample count queries.

#include <metal_stdlib>
using namespace metal;

kernel void ms_read_float(texture2d_ms<float, access::read> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint& samples [[buffer(1)]], uint2 gid [[thread_position_in_grid]]) {
	for (uint s = 0; s < samples; ++s) {
		out[(gid.y * 4 + gid.x) * samples + s] = src.read(gid, s);
	}
}

kernel void ms_read_half(texture2d_ms<half, access::read> src [[texture(0)]], device half4* out [[buffer(0)]], constant uint& samples [[buffer(1)]], uint2 gid [[thread_position_in_grid]]) {
	for (uint s = 0; s < samples; ++s) {
		out[(gid.y * 4 + gid.x) * samples + s] = src.read(gid, s);
	}
}

kernel void ms_read_uint(texture2d_ms<uint, access::read> src [[texture(0)]], device uint4* out [[buffer(0)]], constant uint& samples [[buffer(1)]], uint2 gid [[thread_position_in_grid]]) {
	for (uint s = 0; s < samples; ++s) {
		out[(gid.y * 4 + gid.x) * samples + s] = src.read(gid, s);
	}
}

kernel void ms_read_depth(depth2d_ms<float, access::read> src [[texture(0)]], device float* out [[buffer(0)]], constant uint& samples [[buffer(1)]], uint2 gid [[thread_position_in_grid]]) {
	for (uint s = 0; s < samples; ++s) {
		out[(gid.y * 4 + gid.x) * samples + s] = src.read(gid, s);
	}
}

kernel void ms_read_array(texture2d_ms_array<float, access::read> src [[texture(0)]], device float4* out [[buffer(0)]], constant uint& samples [[buffer(1)]], uint3 gid [[thread_position_in_grid]]) {
	for (uint s = 0; s < samples; ++s) {
		out[((gid.z * 4 + gid.y) * 4 + gid.x) * samples + s] = src.read(gid.xy, gid.z, s);
	}
}

kernel void ms_sizes(texture2d_ms<float, access::read> src [[texture(0)]], device uint* out [[buffer(0)]]) {
	out[0] = src.get_width();
	out[1] = src.get_height();
	out[2] = src.get_num_samples();
}

kernel void ms_array_sizes(texture2d_ms_array<float, access::read> src [[texture(0)]], device uint* out [[buffer(0)]]) {
	out[0] = src.get_width();
	out[1] = src.get_height();
	out[2] = src.get_array_size();
	out[3] = src.get_num_samples();
}
