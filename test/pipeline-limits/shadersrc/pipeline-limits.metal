// Copyright (C) 2026 Darling Developers
//
// The kernels shadersrc/pipeline-limits.ll is written for. No Metal compiler produced
// pipeline-limits.metallib; see README.md.

#include <metal_stdlib>
using namespace metal;

kernel void unlimited(device uint* out [[buffer(0)]], uint2 id [[thread_position_in_grid]], uint2 size [[threads_per_grid]]) {
	out[id.y * size.x + id.x] = id.y * size.x + id.x;
}

[[max_total_threads_per_threadgroup(64)]]
kernel void limited(device uint* out [[buffer(0)]], uint2 id [[thread_position_in_grid]], uint2 size [[threads_per_grid]]) {
	out[id.y * size.x + id.x] = id.y * size.x + id.x;
}

// more than any Metal device allows
[[max_total_threads_per_threadgroup(2048)]]
kernel void too_large(device uint* out [[buffer(0)]], uint2 id [[thread_position_in_grid]], uint2 size [[threads_per_grid]]) {
	out[id.y * size.x + id.x] = id.y * size.x + id.x;
}
