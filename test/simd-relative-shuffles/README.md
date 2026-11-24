# simd-relative-shuffles

Compute kernels that shuffle integers and floats up and down a simdgroup with `simd_shuffle_down` and
`simd_shuffle_up`, which leave the lanes with no lane to read from unchanged. The host compares every
thread's results with what the Metal Shading Language specification defines for the simdgroup width of the
device.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/simd-relative-shuffles.ll` is hand-written AIR, and
`simd-relative-shuffles.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/simd-relative-shuffles.ll -o simd-relative-shuffles.air
    ../air-ops/make-metallib.py simd-relative-shuffles.air simd-relative-shuffles.metallib kernel:shuffle_up_down kernel:shuffle_up_down_float

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
