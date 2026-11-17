// Copyright (C) 2026 Lubos Dolezel
//
// Kernels for the bit-ops test: the bit-counting and bit-field functions of the Metal Shading Language
// and arithmetic on half values.

#include <metal_stdlib>
using namespace metal;

kernel void bit_ops(device const uint* in [[buffer(0)]], device uint* out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
	uint v = in[gid];
	device uint* o = out + gid * 8;
	o[0] = popcount(v);
	o[1] = clz(v);
	o[2] = ctz(v);
	o[3] = reverse_bits(v);
	o[4] = extract_bits(v, 4, 8);
	o[5] = insert_bits(v, 0xAu, 8, 4);
	o[6] = mulhi(v, 0x9E3779B9u);
	o[7] = rotate(v, 5u);
}

kernel void signed_bit_ops(device const int* in [[buffer(0)]], device int* out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
	int v = in[gid];
	device int* o = out + gid * 4;
	o[0] = popcount(v);
	o[1] = extract_bits(v, 4, 8);
	o[2] = clz(v);
	o[3] = mulhi(v, -3);
}

kernel void half_ops(device const float* in [[buffer(0)]], device float* out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
	half a = half(in[gid]);
	half b = half(2.5);
	half4 v = half4(a, b, a + b, a * b);
	out[gid * 4 + 0] = float(a * b + 0.5h);
	out[gid * 4 + 1] = float(max(a, b));
	out[gid * 4 + 2] = float(dot(v, v));
	out[gid * 4 + 3] = float(floor(a) + abs(b));
}
