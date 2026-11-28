# wide-vector-bitcast

A compute kernel that bitcasts integers and vectors of integers and floats to vectors of more than four
elements (`<12 x i8>`, `<6 x i16>`, `<8 x i8>`, `<6 x half>`) and extracts elements from them, at
constant and computed indices. Without the `Vector16` capability, SPIR-V vectors have at most four
components, so Iridium takes each element's bits from the bitcast's operand. The host compares every
element with the bytes it is made of.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/wide-vector-bitcast.ll` is hand-written AIR, and
`wide-vector-bitcast.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/wide-vector-bitcast.ll -o wide-vector-bitcast.air
    ../air-ops/make-metallib.py wide-vector-bitcast.air wide-vector-bitcast.metallib kernel:wide_vector_bitcast

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
