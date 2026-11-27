// Copyright (C) 2026 Darling Developers
//
// Distributed under the terms in the LICENSE file at the root of this repository.
#include <metal_stdlib>
using namespace metal;

// A class (not a struct) of the buffer argument: its LLVM type is named "class.Counters".
class Counters {
public:
	uint count;
	float scale;
};

struct Quad {
	float a;
	float b;
	float c;
	float d;
};

struct Wide {
	float4 value;
};

// Reads a local object through a pointer of another type: a struct of four floats as a float4, and a float3 inside
// a float4.
kernel void reinterpret_load(device float4* out [[buffer(0)]], constant float4* in [[buffer(1)]],
	device Counters* counters [[buffer(2)]], uint tid [[thread_position_in_grid]]) {
	Quad q;
	q.a = in[tid].x;
	q.b = in[tid].y;
	q.c = in[tid].z;
	q.d = in[tid].w * counters->scale;
	thread float4* asVector = reinterpret_cast<thread float4*>(&q);
	out[tid] = *asVector;
	counters->count = 7;
}

// Writes a local object through a pointer of another type.
kernel void reinterpret_store(device Quad* out [[buffer(0)]], constant float4* in [[buffer(1)]],
	uint tid [[thread_position_in_grid]]) {
	Wide w;
	w.value = float4(0.0);
	thread Quad* asQuad = reinterpret_cast<thread Quad*>(&w);
	asQuad->a = in[tid].x;
	asQuad->b = in[tid].y;
	asQuad->c = in[tid].z;
	asQuad->d = in[tid].w;
	out[tid].a = w.value.x;
	out[tid].b = w.value.y;
	out[tid].c = w.value.z;
	out[tid].d = w.value.w;
}
