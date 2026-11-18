// Copyright (C) 2026 Lubos Dolezel
//
// Kernels for the texture-size test: the size, layer count and level count queries of each texture class.

#include <metal_stdlib>
using namespace metal;

kernel void size_1d(texture1d<float> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_num_mip_levels();
}

kernel void size_1d_array(texture1d_array<float> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_array_size();
}

kernel void size_2d(texture2d<float> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_height();
	o[2] = t.get_width(1);
	o[3] = t.get_height(2);
	o[4] = t.get_num_mip_levels();
}

kernel void size_2d_array(texture2d_array<float> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_height();
	o[2] = t.get_array_size();
}

kernel void size_3d(texture3d<float> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_height();
	o[2] = t.get_depth();
	o[3] = t.get_depth(1);
}

kernel void size_cube(texturecube<float> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_height();
	o[2] = t.get_num_mip_levels();
}

kernel void size_depth(depth2d<float> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_height();
	o[2] = t.get_num_mip_levels();
}

kernel void size_depth_cube(depthcube<float> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_height();
}

kernel void size_depth_array(depth2d_array<float> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_height();
	o[2] = t.get_array_size();
}

kernel void size_write_2d(texture2d<float, access::write> t [[texture(0)]], device uint* o [[buffer(0)]]) {
	o[0] = t.get_width();
	o[1] = t.get_height();
	o[2] = t.get_num_mip_levels();
}
