# round-half-away

Compute kernels that call `air.round`, MSL's `round`, on `float`, `half` and `<2 x float>`, and the fast
variant on `float`. The host compares the results bit for bit with `round` as the Metal Shading Language
specification defines it: halfway cases are rounded away from zero.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/round-half-away.ll` is hand-written AIR, and `round-half-away.metallib`
wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/round-half-away.ll -o round-half-away.air
    ../air-ops/make-metallib.py round-half-away.air round-half-away.metallib kernel:round_f32 kernel:round_f16

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
