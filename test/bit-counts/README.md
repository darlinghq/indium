# bit-counts

Compute kernels that call the Metal Shading Language's `popcount`, `clz`, `ctz` and `reverse_bits` on
8, 16 and 64-bit integers (`air.popcount.i8`, `air.clz.i64`, ...), and `clz` and `ctz` on a
two-element vector of 16-bit integers. Vulkan allows the SPIR-V instructions these become
(`OpBitCount`, `OpBitReverse`, GLSL.std.450 `FindUMsb` and `FindILsb`) only on 32-bit integers, so
Iridium computes them on 32-bit words. The host compares every result with the definition in the
specification. The 32-bit functions are in the bit-ops test.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/bit-counts.ll` is hand-written AIR, and `bit-counts.metallib`
wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/bit-counts.ll -o bit-counts.air
    ../air-ops/make-metallib.py bit-counts.air bit-counts.metallib kernel:counts_i8 kernel:counts_i16 kernel:counts_i64

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
