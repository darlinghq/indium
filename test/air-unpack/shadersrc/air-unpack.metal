// Copyright (C) 2026 Lubos Dolezel
//
// The kernel that air-unpack.ll is the AIR of.

#include <metal_stdlib>
using namespace metal;

kernel void unpack(device float* out [[buffer(0)]], device const uint* in [[buffer(1)]], uint id [[thread_position_in_grid]]) {
	uint packed = in[id];
	float4 a = unpack_unorm4x8_to_float(packed);
	float4 b = unpack_snorm4x8_to_float(packed);
	float2 c = unpack_unorm2x16_to_float(packed);
	float2 d = unpack_snorm2x16_to_float(packed);
	float4 e = unpack_unorm4x8_srgb_to_float(packed);
	device float* o = out + id * 16;
	o[0] = a.x; o[1] = a.y; o[2] = a.z; o[3] = a.w;
	o[4] = b.x; o[5] = b.y; o[6] = b.z; o[7] = b.w;
	o[8] = c.x; o[9] = c.y;
	o[10] = d.x; o[11] = d.y;
	o[12] = e.x; o[13] = e.y; o[14] = e.z; o[15] = e.w;
}
