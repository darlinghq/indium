#include <metal_stdlib>
using namespace metal;

// writes the arguments of an indirect dispatch: four threadgroups wide
kernel void fillArguments(device uint* args [[buffer(0)]]) {
	args[0] = 4;
	args[1] = 1;
	args[2] = 1;
}

// every thread stores its own position plus one, so the number and the identity of the threads that ran can be read back
kernel void mark(device uint* out [[buffer(0)]], uint gid [[thread_position_in_grid]]) {
	out[gid] = gid + 1;
}
