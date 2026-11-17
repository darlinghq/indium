// Copyright (C) 2026 Lubos Dolezel
//
// Kernels for the texture-write test: write() on access::write and access::read_write textures of
// the dimensions and component types the test checks.

#include <metal_stdlib>
using namespace metal;

kernel void write_2d(texture2d<float, access::write> out [[texture(0)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(float4(float(gid.x), float(gid.y), float(gid.x + gid.y), 1.0), gid);
}

kernel void write_2d_uint(texture2d<uint, access::write> out [[texture(0)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(uint4(gid.x, gid.y, gid.x * 10 + gid.y, 7), gid);
}

kernel void write_2d_int(texture2d<int, access::write> out [[texture(0)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(int4(-int(gid.x), int(gid.y), -3, 5), gid);
}

kernel void write_2d_half(texture2d<half, access::write> out [[texture(0)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(half4(half(gid.x), half(gid.y), 0.5h, 1.0h), gid);
}

kernel void write_1d(texture1d<float, access::write> out [[texture(0)]], uint gid [[thread_position_in_grid]]) {
	out.write(float4(float(gid), 2.0 * float(gid), 0.0, 1.0), gid);
}

kernel void write_3d(texture3d<float, access::write> out [[texture(0)]], uint3 gid [[thread_position_in_grid]]) {
	out.write(float4(float(gid.x), float(gid.y), float(gid.z), 1.0), gid);
}

kernel void write_2d_array(texture2d_array<float, access::write> out [[texture(0)]], uint3 gid [[thread_position_in_grid]]) {
	out.write(float4(float(gid.x), float(gid.y), float(gid.z), 1.0), gid.xy, gid.z);
}

kernel void write_1d_array(texture1d_array<float, access::write> out [[texture(0)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(float4(float(gid.x), float(gid.y), 0.0, 1.0), gid.x, gid.y);
}

kernel void read_write_2d(texture2d<float, access::read_write> t [[texture(0)]], uint2 gid [[thread_position_in_grid]]) {
	float4 v = t.read(gid);
	t.write(v * 2.0 + float4(1.0, 0.0, 0.0, 0.0), gid);
}

kernel void copy_read_to_write(texture2d<float, access::read> in [[texture(0)]], texture2d<float, access::write> out [[texture(1)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(in.read(gid).wzyx, gid);
}


kernel void copy_1d(texture1d<float, access::read> in [[texture(0)]], texture1d<float, access::write> out [[texture(1)]], uint gid [[thread_position_in_grid]]) {
	out.write(in.read(gid), gid);
}

kernel void copy_3d(texture3d<float, access::read> in [[texture(0)]], texture3d<float, access::write> out [[texture(1)]], uint3 gid [[thread_position_in_grid]]) {
	out.write(in.read(gid), gid);
}

kernel void copy_2d_array(texture2d_array<float, access::read> in [[texture(0)]], texture2d_array<float, access::write> out [[texture(1)]], uint3 gid [[thread_position_in_grid]]) {
	out.write(in.read(gid.xy, gid.z), gid.xy, gid.z);
}

kernel void copy_1d_array(texture1d_array<float, access::read> in [[texture(0)]], texture1d_array<float, access::write> out [[texture(1)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(in.read(gid.x, gid.y), gid.x, gid.y);
}

kernel void copy_2d_uint(texture2d<uint, access::read> in [[texture(0)]], texture2d<uint, access::write> out [[texture(1)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(in.read(gid) + 1, gid);
}

kernel void copy_2d_half(texture2d<half, access::read> in [[texture(0)]], texture2d<half, access::write> out [[texture(1)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(in.read(gid), gid);
}

// reads level 1 of the input into the (half-sized) output
kernel void copy_2d_level(texture2d<float, access::read> in [[texture(0)]], texture2d<float, access::write> out [[texture(1)]], uint2 gid [[thread_position_in_grid]]) {
	out.write(in.read(gid, 1), gid);
}

kernel void read_write_1d(texture1d<float, access::read_write> t [[texture(0)]], uint gid [[thread_position_in_grid]]) {
	t.write(t.read(gid) + 1.0, gid);
}

kernel void read_write_2d_array(texture2d_array<float, access::read_write> t [[texture(0)]], uint3 gid [[thread_position_in_grid]]) {
	t.write(t.read(gid.xy, gid.z) + 1.0, gid.xy, gid.z);
}

kernel void size_2d(texture2d<float, access::write> t [[texture(0)]], device uint* out [[buffer(0)]], uint gid [[thread_position_in_grid]]) {
	out[0] = t.get_width();
	out[1] = t.get_height();
}
