# simd-narrow-shuffles

Compute kernels that broadcast and shuffle 8 and 16-bit integers (signed and unsigned) and halfs across a
simdgroup: `simd_shuffle`, `simd_shuffle_xor`, `simd_shuffle_down`, `simd_shuffle_up`, `simd_broadcast` from a
constant and from a computed lane, and `simd_broadcast_first`. The host compares every thread's results with what
the Metal Shading Language specification defines for the simdgroup width of the device.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/simd-narrow-shuffles.ll` is hand-written AIR, and
`simd-narrow-shuffles.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/simd-narrow-shuffles.ll -o simd-narrow-shuffles.air
    ../air-ops/make-metallib.py simd-narrow-shuffles.air simd-narrow-shuffles.metallib kernel:shuffle_s_i8 kernel:shuffle_u_i8 kernel:shuffle_s_i16 kernel:shuffle_u_i16 kernel:shuffle_f16

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
