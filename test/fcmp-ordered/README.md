# fcmp-ordered

Compute kernels that compare `float`, `half` and `<2 x float>` values with `fcmp ord` and `fcmp uno`: two
values, a value with itself (how LLVM tests for a NaN) and a value with a constant, with NaNs of both signs,
infinities, zeros, a denormal and ordinary numbers. The host compares the results with the comparisons as LLVM
defines them.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/fcmp-ordered.ll` is hand-written AIR, and `fcmp-ordered.metallib`
wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/fcmp-ordered.ll -o fcmp-ordered.air
    ../air-ops/make-metallib.py fcmp-ordered.air fcmp-ordered.metallib kernel:ordered_f32 kernel:ordered_f16 kernel:ordered_v2f32

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
