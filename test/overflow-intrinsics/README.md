# overflow-intrinsics

Compute kernels that call LLVM's arithmetic intrinsics with an overflow check,
`llvm.<s|u><add|sub|mul>.with.overflow`, on 16, 32 and 64-bit integers and on a vector of 32-bit
integers. Each returns the wrapped result and whether the operation overflowed. The host compares both with the intrinsics as LLVM
defines them.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/overflow-intrinsics.ll` is hand-written AIR, and
`overflow-intrinsics.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/overflow-intrinsics.ll -o overflow-intrinsics.air
    ../air-ops/make-metallib.py overflow-intrinsics.air overflow-intrinsics.metallib kernel:overflow_i16 kernel:overflow_i32 kernel:overflow_i64

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
