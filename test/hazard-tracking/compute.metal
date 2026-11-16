#include <metal_stdlib>
using namespace metal;

// data[i] = data[i] * 3 + 1: a chain of dispatches over one buffer only gives the right numbers if each one sees
// what the one before it wrote
kernel void step(device uint* data [[buffer(0)]], uint gid [[thread_position_in_grid]]) {
	data[gid] = data[gid] * 3 + 1;
}

// out[i] = in[i] + 1
kernel void addOne(device uint* in [[buffer(0)]], device uint* out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
	out[gid] = in[gid] + 1;
}

// the arguments of an indirect draw of the three vertices of a triangle
kernel void fillDrawArguments(device uint* args [[buffer(0)]]) {
	args[0] = 3;
	args[1] = 1;
	args[2] = 0;
	args[3] = 0;
}
