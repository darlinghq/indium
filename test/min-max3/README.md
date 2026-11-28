# min-max3

Compute kernels that call `air.min3`, `air.max3` and `air.median3`, MSL's integer `min3`, `max3` and
`median3`, signed and unsigned, on 8, 16, 32 and 64-bit integers and on `<2 x i32>`. The host compares the
results with the functions as the Metal Shading Language specification defines them.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/min-max3.ll` is hand-written AIR, and `min-max3.metallib` wraps it
in the MTLB container Iridium reads:

    llvm-as shadersrc/min-max3.ll -o min-max3.air
    ../air-ops/make-metallib.py min-max3.air min-max3.metallib kernel:minmax3_i8 kernel:minmax3_i16 kernel:minmax3_i32 kernel:minmax3_i64 kernel:minmax3_v2i32

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
