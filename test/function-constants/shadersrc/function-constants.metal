// Copyright (C) 2026 Lubos Dolezel
//
// Function constants of every type the translator supports, a constant derived from others, a test of
// whether a constant was given, and an argument that exists only when a constant is true.
#include <metal_stdlib>
using namespace metal;

constant bool use_scale [[function_constant(0)]];
constant int mode [[function_constant(1)]];
constant float factor [[function_constant(2)]];
constant uint opt [[function_constant(3)]];
constant bool has_opt = is_function_constant_defined(opt);
constant float scaled = use_scale ? factor : 1.0;

// out[tid] = mode * (use_scale ? factor : 1) + (opt defined ? opt : 0) + (use_scale ? extra[tid] : 0)
kernel void fc_kernel(device float* out [[buffer(0)]],
                      device const float* extra [[buffer(1), function_constant(use_scale)]],
                      uint tid [[thread_position_in_grid]]) {
	float v = float(mode) * scaled;
	if (has_opt) v += float(opt);
	if (use_scale) v += extra[tid];
	out[tid] = v;
}

// out[tid] = mode * factor
kernel void fc_simple(device float* out [[buffer(0)]], uint tid [[thread_position_in_grid]]) {
	out[tid] = float(mode) * factor;
}
