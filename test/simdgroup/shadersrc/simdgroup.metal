// Copyright (C) 2026 Lubos Dolezel
//
// Kernels for the simdgroup test: the simdgroup built-ins, the reductions, prefix scans, broadcasts, shuffles, votes
// and ballots of the Metal Shading Language, and simdgroup_barrier(). Each thread of one threadgroup stores what it
// computed; the width of a simdgroup is whatever the device uses, so the kernels take positions within the simdgroup
// from the built-ins.

#include <metal_stdlib>
using namespace metal;

kernel void simd_ids(device uint* out [[buffer(0)]], uint t [[thread_position_in_threadgroup]], uint lane [[thread_index_in_simdgroup]], uint group [[simdgroup_index_in_threadgroup]], uint groups [[simdgroups_per_threadgroup]], uint width [[threads_per_simdgroup]]) {
	out[t * 4] = lane;
	out[t * 4 + 1] = group;
	out[t * 4 + 2] = groups;
	out[t * 4 + 3] = width;
}

// ushort versions of the built-ins
kernel void simd_ids_short(device uint* out [[buffer(0)]], uint t [[thread_position_in_threadgroup]], ushort lane [[thread_index_in_simdgroup]], ushort width [[threads_per_simdgroup]]) {
	out[t * 2] = lane;
	out[t * 2 + 1] = width;
}

kernel void simd_reduce(device uint* out [[buffer(0)]], uint t [[thread_position_in_threadgroup]]) {
	out[t * 6] = simd_sum(t);
	out[t * 6 + 1] = simd_min(t + 3);
	out[t * 6 + 2] = simd_max(t);
	out[t * 6 + 3] = simd_and(t | 0xF0u);
	out[t * 6 + 4] = simd_or(t & 3u);
	out[t * 6 + 5] = simd_xor(t);
}

kernel void simd_reduce_float(device float* out [[buffer(0)]], uint t [[thread_position_in_threadgroup]]) {
	out[t * 4] = simd_sum(float(t) * 0.5f);
	out[t * 4 + 1] = simd_product(1.0f + float(t & 1));
	out[t * 4 + 2] = simd_min(float(t) - 4.0f);
	out[t * 4 + 3] = simd_max(float(t) * 2.0f);
}

kernel void simd_reduce_signed(device int* out [[buffer(0)]], uint t [[thread_position_in_threadgroup]]) {
	int v = int(t) - 20;
	out[t * 2] = simd_min(v);
	out[t * 2 + 1] = simd_max(v);
}

kernel void simd_prefix(device uint* out [[buffer(0)]], device float* outFloat [[buffer(1)]], uint t [[thread_position_in_threadgroup]]) {
	out[t * 2] = simd_prefix_inclusive_sum(t);
	out[t * 2 + 1] = simd_prefix_exclusive_sum(t);
	outFloat[t] = simd_prefix_inclusive_sum(float(t) + 0.5f);
}

kernel void simd_broadcast_ops(device uint* out [[buffer(0)]], uint t [[thread_position_in_threadgroup]], uint width [[threads_per_simdgroup]]) {
	out[t * 3] = simd_broadcast(t, 3);
	out[t * 3 + 1] = simd_broadcast_first(t);
	// a lane that is not a constant
	out[t * 3 + 2] = simd_broadcast(t, ushort(width - 1));
}

kernel void simd_shuffle_ops(device uint* out [[buffer(0)]], uint t [[thread_position_in_threadgroup]], uint lane [[thread_index_in_simdgroup]], uint width [[threads_per_simdgroup]]) {
	out[t * 4] = simd_shuffle(t, ushort((lane + 1) % width));
	out[t * 4 + 1] = simd_shuffle_xor(t, ushort(1));
	out[t * 4 + 2] = simd_shuffle_down(t, ushort(1));
	out[t * 4 + 3] = simd_shuffle_up(t, ushort(1));
}

kernel void simd_vote_ops(device uint* out [[buffer(0)]], uint lane [[thread_index_in_simdgroup]], uint width [[threads_per_simdgroup]], uint t [[thread_position_in_threadgroup]]) {
	out[t * 4] = simd_all(lane < width);
	out[t * 4 + 1] = simd_all(lane < 3);
	out[t * 4 + 2] = simd_any(lane == 2);
	out[t * 4 + 3] = simd_any(lane >= width);
}

kernel void simd_ballot_ops(device uint* out [[buffer(0)]], uint lane [[thread_index_in_simdgroup]], uint t [[thread_position_in_threadgroup]]) {
	ulong odd = ulong(simd_ballot((lane & 1) != 0));
	ulong active = ulong(simd_active_threads_mask());
	out[t * 4] = uint(odd);
	out[t * 4 + 1] = uint(odd >> 32);
	out[t * 4 + 2] = uint(active);
	out[t * 4 + 3] = uint(active >> 32);
}

kernel void simd_barriers(device uint* out [[buffer(0)]], uint t [[thread_position_in_threadgroup]]) {
	out[t * 3] = 1u;
	simdgroup_barrier(mem_flags::mem_none);
	out[t * 3 + 1] = 2u;
	simdgroup_barrier(mem_flags::mem_device);
	out[t * 3 + 2] = 3u;
}
