# bool-conversions

Compute kernels that convert AIR's `i1`, which Iridium translates to a SPIR-V boolean, to other
types: `sext` and `zext` to 8, 16, 32 and 64 bits, and `uitofp` and `sitofp` to `float` and `half`,
each also on a two-element vector. The SPIR-V conversion instructions take no booleans, so these
become `OpSelect`s. The host compares every result with the conversion as LLVM defines it.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/bool-conversions.ll` is hand-written AIR, and
`bool-conversions.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/bool-conversions.ll -o bool-conversions.air
    ../air-ops/make-metallib.py bool-conversions.air bool-conversions.metallib kernel:extend_bool kernel:bool_to_float

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
