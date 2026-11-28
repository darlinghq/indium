# math-functions

Compute kernels that call the Metal math functions Iridium translates into sequences rather than a single
GLSL.std.450 instruction: `sinpi`, `cospi` and `tanpi`, `exp10` and `log10`, `ldexp`, `nextafter`, and
`sincos`, which returns the cosine through a pointer; and the inverse hyperbolic functions, which map onto
one. They run on `float`, `half` and vectors, along with `llvm.copysign`. The host compares the results with
the functions as the Metal Shading Language specification defines them, bit for bit where the result is
exact.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/math-functions.ll` is hand-written AIR, and
`math-functions.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/math-functions.ll -o math-functions.air
    ../air-ops/make-metallib.py math-functions.air math-functions.metallib kernel:functions_f32 kernel:functions_f16 kernel:ldexp_f32 kernel:ldexp_f16 kernel:nextafter_f32 kernel:nextafter_f16 kernel:pi_f32 kernel:pi_f16 kernel:vector_v2f32

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
