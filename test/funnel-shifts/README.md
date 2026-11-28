# funnel-shifts

Compute kernels that call LLVM's funnel shifts, `llvm.fshl` and `llvm.fshr`, on 8, 16, 32 and 64-bit
integers and on `<2 x i32>`, with shift amounts of zero, of multiples of the width and larger than it. The
host compares the results with the intrinsics as LLVM defines them.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/funnel-shifts.ll` is hand-written AIR, and `funnel-shifts.metallib`
wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/funnel-shifts.ll -o funnel-shifts.air
    ../air-ops/make-metallib.py funnel-shifts.air funnel-shifts.metallib kernel:funnel_i8 kernel:funnel_i16 kernel:funnel_i32 kernel:funnel_i64 kernel:funnel_v2i32

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
