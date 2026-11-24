// Copyright (C) 2026 Darling Developers
//
// Reads a function constant of every scalar type, and which of them were given a value.
#include <metal_stdlib>
using namespace metal;

constant bool cb [[function_constant(0)]];
constant int ci [[function_constant(1)]];
constant uint cu [[function_constant(2)]];
constant float cf [[function_constant(3)]];
constant char cc [[function_constant(4)]];
constant uchar cuc [[function_constant(5)]];
constant short cs [[function_constant(6)]];
constant ushort cus [[function_constant(7)]];
constant half ch [[function_constant(8)]];
constant long cl [[function_constant(9)]];
constant ulong cul [[function_constant(10)]];

kernel void types(device ulong* out [[buffer(0)]]) {
    out[0] = cb ? 1 : 0;
    out[1] = ulong(ci);
    out[2] = cu;
    out[3] = ulong(cf * 4.0f);
    out[4] = ulong(cc);
    out[5] = cuc;
    out[6] = ulong(cs);
    out[7] = cus;
    out[8] = ulong(float(ch) * 4.0f);
    out[9] = ulong(cl);
    out[10] = cul;
    out[11] = (is_function_constant_defined(cb) ? 1 : 0) | (is_function_constant_defined(ci) ? 2 : 0)
        | (is_function_constant_defined(cu) ? 4 : 0) | (is_function_constant_defined(cf) ? 8 : 0)
        | (is_function_constant_defined(cc) ? 16 : 0) | (is_function_constant_defined(cuc) ? 32 : 0)
        | (is_function_constant_defined(cs) ? 64 : 0) | (is_function_constant_defined(cus) ? 128 : 0)
        | (is_function_constant_defined(ch) ? 256 : 0) | (is_function_constant_defined(cl) ? 512 : 0)
        | (is_function_constant_defined(cul) ? 1024 : 0);
}
