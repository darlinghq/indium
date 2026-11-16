#include <metal_stdlib>
using namespace metal;

// a kernel with a barrier cannot skip threads, so it cannot be dispatched over a partial threadgroup
kernel void markBarrier(device uint* out [[buffer(0)]], uint gid [[thread_position_in_grid]]) {
	threadgroup_barrier(mem_flags::mem_device);
	out[gid] = gid + 1;
}
