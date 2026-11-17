// Copyright (C) 2026 Lubos Dolezel
//
// Kernels for the atomics test: the atomic functions on device and threadgroup memory.

#include <metal_stdlib>
using namespace metal;

// every thread adds 1 to a counter and its own id to a sum
kernel void add_counters(device atomic_uint* counters [[buffer(0)]], uint gid [[thread_position_in_grid]]) {
	atomic_fetch_add_explicit(&counters[0], 1u, memory_order_relaxed);
	atomic_fetch_add_explicit(&counters[1], gid, memory_order_relaxed);
	atomic_fetch_sub_explicit(&counters[2], 1u, memory_order_relaxed);
}

// the other read-modify-write operations on unsigned values: counters[0] min, [1] max, [2] or, [3] and, [4] xor
kernel void bit_counters(device atomic_uint* counters [[buffer(0)]], uint gid [[thread_position_in_grid]]) {
	atomic_fetch_min_explicit(&counters[0], gid + 5u, memory_order_relaxed);
	atomic_fetch_max_explicit(&counters[1], gid + 5u, memory_order_relaxed);
	atomic_fetch_or_explicit(&counters[2], 1u << (gid % 32u), memory_order_relaxed);
	atomic_fetch_and_explicit(&counters[3], ~(1u << (gid % 32u)), memory_order_relaxed);
	atomic_fetch_xor_explicit(&counters[4], 1u << (gid % 32u), memory_order_relaxed);
}

// signed min and max
kernel void signed_counters(device atomic_int* counters [[buffer(0)]], uint gid [[thread_position_in_grid]]) {
	int v = int(gid) - 20;
	atomic_fetch_min_explicit(&counters[0], v, memory_order_relaxed);
	atomic_fetch_max_explicit(&counters[1], v, memory_order_relaxed);
}

// each thread takes a ticket (the value before its increment); the tickets are written out
kernel void tickets(device atomic_uint* counter [[buffer(0)]], device uint* out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
	out[gid] = atomic_fetch_add_explicit(&counter[0], 1u, memory_order_relaxed);
}

// load, store, exchange: the thread with gid 0 stores, everyone reads after the barrier of the dispatch is not
// available, so only exchange is checked: each thread swaps its own id in and records the id it got out
kernel void exchange(device atomic_uint* slot [[buffer(0)]], device uint* out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
	out[gid] = atomic_exchange_explicit(&slot[0], gid + 1u, memory_order_relaxed);
}

// compare_exchange: a spin-free claim, only the first thread to arrive sees the expected value
kernel void claim(device atomic_uint* flag [[buffer(0)]], device uint* out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
	uint expected = 0;
	bool won = atomic_compare_exchange_weak_explicit(&flag[0], &expected, gid + 1u, memory_order_relaxed, memory_order_relaxed);
	out[gid] = won ? 1u : 0u;
}

kernel void load_store(device atomic_uint* slots [[buffer(0)]], device uint* out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
	atomic_store_explicit(&slots[gid], gid * 3u + 1u, memory_order_relaxed);
	out[gid] = atomic_load_explicit(&slots[gid], memory_order_relaxed);
}

// threadgroup atomics: each group counts its own threads and sums their ids
kernel void group_counters(device uint* out [[buffer(0)]], uint gid [[thread_position_in_grid]], uint lid [[thread_position_in_threadgroup]], uint group [[threadgroup_position_in_grid]]) {
	threadgroup atomic_uint count;
	threadgroup atomic_uint sum;
	if (lid == 0) {
		atomic_store_explicit(&count, 0u, memory_order_relaxed);
		atomic_store_explicit(&sum, 0u, memory_order_relaxed);
	}
	threadgroup_barrier(mem_flags::mem_threadgroup);
	atomic_fetch_add_explicit(&count, 1u, memory_order_relaxed);
	atomic_fetch_add_explicit(&sum, lid, memory_order_relaxed);
	threadgroup_barrier(mem_flags::mem_threadgroup);
	if (lid == 0) {
		out[group * 2] = atomic_load_explicit(&count, memory_order_relaxed);
		out[group * 2 + 1] = atomic_load_explicit(&sum, memory_order_relaxed);
	}
}

kernel void claim_from_seven(device atomic_uint* flag [[buffer(0)]], device uint* out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
	uint expected = 7;
	bool won = atomic_compare_exchange_weak_explicit(&flag[0], &expected, gid + 100u, memory_order_relaxed, memory_order_relaxed);
	out[gid] = won ? 1u : 0u;
	// a failed exchange leaves the value found in memory in `expected`
	if (!won) {
		out[gid] = 1000u + expected;
	}
}
