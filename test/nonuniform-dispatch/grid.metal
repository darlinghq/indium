#include <metal_stdlib>
using namespace metal;

// every thread that runs stores its position plus one, so the threads that ran can be read back
kernel void mark1D(device uint* out [[buffer(0)]], uint gid [[thread_position_in_grid]]) {
	out[gid] = gid + 1;
}

// the grid is five threads wide
kernel void mark2D(device uint* out [[buffer(0)]], uint2 gid [[thread_position_in_grid]]) {
	out[gid.y * 5 + gid.x] = 1 + gid.y * 10 + gid.x;
}

kernel void mark3D(device uint* out [[buffer(0)]], uint3 gid [[thread_position_in_grid]]) {
	out[(gid.z * 3 + gid.y) * 5 + gid.x] = 1 + gid.z * 100 + gid.y * 10 + gid.x;
}
