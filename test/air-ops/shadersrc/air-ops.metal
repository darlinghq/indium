// Copyright (C) 2026 Darling Developers
//
// The kernels shadersrc/air-ops.ll is written for. No Metal compiler produced air-ops.metallib;
// see README.md.

#include <metal_stdlib>
using namespace metal;

constant float table[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

kernel void int_ops(device int* out [[buffer(0)]], device const int* a [[buffer(1)]], device const int* b [[buffer(2)]], uint id [[thread_position_in_grid]]) {
	int x = a[id], y = b[id];
	uint ux = x, uy = y, shift = y & 7;
	device int* r = out + id * 18;
	r[0] = ux / uy;
	r[1] = ux % uy;
	r[2] = x / y;
	r[3] = x % y;
	r[4] = ux << shift;
	r[5] = ux >> shift;
	r[6] = x >> shift;
	r[7] = x & y;
	r[8] = x | y;
	r[9] = x ^ y;
	r[10] = x < y;
	r[11] = ux < uy ? x : y;
	r[12] = min(x, y);
	r[13] = max(ux, uy);
	r[14] = abs(x);
	r[15] = short(x);
	r[16] = clamp(x, -5, 5);
	r[17] = uchar(x);
}

kernel void float_ops(device float* out [[buffer(0)]], device const float* xs [[buffer(1)]], device const float* ys [[buffer(2)]], uint id [[thread_position_in_grid]]) {
	float x = xs[id], y = ys[id], ax = fabs(x);
	device float* r = out + id * 16;
	r[0] = -x;
	r[1] = fmod(x, y);
	r[2] = sqrt(ax);
	r[3] = floor(x);
	r[4] = fmin(x, y);
	r[5] = fma(x, y, 1.0f);
	r[6] = float(int(x));
	r[7] = float(uint(ax));
	r[8] = x >= y ? 1.0f : 0.0f;
	r[9] = x <= y ? 1.0f : 0.0f;
	r[10] = x != y ? 1.0f : 0.0f;
	r[11] = x == y ? 1.0f : 0.0f;
	r[12] = float(int(x) * 2);
	r[13] = float(uint(ax));
	r[14] = clamp(x, -1.0f, 1.0f);
	r[15] = fract(ax);
}

kernel void table_lookup(device float* out [[buffer(0)]], uint id [[thread_position_in_grid]]) {
	out[id] = table[id];
}
