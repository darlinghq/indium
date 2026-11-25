#include <metal_stdlib>
using namespace metal;

// the threadgroup's elements of x, in reverse order
kernel void reverse(device float *x [[buffer(0)]], threadgroup float *tmp [[threadgroup(0)]],
                    uint i [[thread_position_in_threadgroup]], uint n [[threads_per_threadgroup]]) {
	tmp[i] = x[i];
	threadgroup_barrier(mem_flags::mem_threadgroup);
	x[i] = tmp[n - 1 - i];
}

// two arguments with elements of different sizes, declared out of index order: out[i] = 11 * (n - 1 - i)
kernel void pair(device uint *out [[buffer(0)]], threadgroup uint4 *wide [[threadgroup(1)]], threadgroup uint *narrow [[threadgroup(0)]],
                 uint i [[thread_position_in_threadgroup]], uint n [[threads_per_threadgroup]]) {
	wide[n - 1 - i] = uint4(i, i, i, i * 10);
	narrow[i] = i;
	threadgroup_barrier(mem_flags::mem_threadgroup);
	out[i] = wide[i].w + narrow[n - 1 - i];
}
