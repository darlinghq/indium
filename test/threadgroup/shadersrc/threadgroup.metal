// Copyright (C) 2026 Lubos Dolezel
//
// Kernels for the threadgroup test: threadgroup memory declared in the kernel body and
// threadgroup_barrier().

#include <metal_stdlib>
using namespace metal;

kernel void reverse_tile(device const uint* in [[buffer(0)]], device uint* out [[buffer(1)]], uint gid [[thread_position_in_grid]], uint lid [[thread_position_in_threadgroup]], uint size [[threads_per_threadgroup]]) {
	threadgroup uint tile[64];
	tile[lid] = in[gid];
	threadgroup_barrier(mem_flags::mem_threadgroup);
	out[gid] = tile[size - 1 - lid];
}

kernel void sum_tile(device const uint* in [[buffer(0)]], device uint* out [[buffer(1)]], uint gid [[thread_position_in_grid]], uint lid [[thread_position_in_threadgroup]], uint group [[threadgroup_position_in_grid]], uint size [[threads_per_threadgroup]]) {
	threadgroup uint scratch[64];
	scratch[lid] = in[gid];
	threadgroup_barrier(mem_flags::mem_threadgroup);
	for (uint stride = size / 2; stride > 0; stride /= 2) {
		if (lid < stride) {
			scratch[lid] += scratch[lid + stride];
		}
		threadgroup_barrier(mem_flags::mem_threadgroup);
	}
	if (lid == 0) {
		out[group] = scratch[0];
	}
}

kernel void float_tile(device const float* in [[buffer(0)]], device float* out [[buffer(1)]], uint gid [[thread_position_in_grid]], uint lid [[thread_position_in_threadgroup]]) {
	threadgroup float2 pairs[32];
	threadgroup float total;
	pairs[lid % 32] = float2(in[gid], 2.0 * in[gid]);
	threadgroup_barrier(mem_flags::mem_threadgroup);
	if (lid == 0) {
		total = 0.0;
		for (uint i = 0; i < 32; ++i) {
			total += pairs[i].x + pairs[i].y;
		}
	}
	threadgroup_barrier(mem_flags::mem_threadgroup | mem_flags::mem_device);
	out[gid] = total;
}

kernel void builtins(device const uint* in [[buffer(0)]], device uint* out [[buffer(1)]], uint3 gid3 [[thread_position_in_grid]], uint3 lid [[thread_position_in_threadgroup]], uint3 group [[threadgroup_position_in_grid]], uint3 size [[threads_per_threadgroup]], uint3 groups [[threadgroups_per_grid]], uint3 grid [[threads_per_grid]], uint index [[thread_index_in_threadgroup]]) {
	uint gid = gid3.x;
	out[gid * 8 + 0] = lid.x;
	out[gid * 8 + 1] = group.x;
	out[gid * 8 + 2] = size.x;
	out[gid * 8 + 3] = groups.x;
	out[gid * 8 + 4] = grid.x;
	out[gid * 8 + 5] = index;
	out[gid * 8 + 6] = lid.y + lid.z + group.y + group.z;
	out[gid * 8 + 7] = size.y * 100 + size.z;
}
