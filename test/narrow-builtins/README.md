# narrow-builtins

A compute kernel whose `thread_position_in_grid`, `thread_index_in_threadgroup` and
`threadgroup_position_in_grid` parameters are 16-bit integers (MSL's `ushort3`, `ushort` and
`ushort2`). The Vulkan builtins are 32-bit, so Iridium narrows them with `OpUConvert`, whose result
type must be unsigned. The kernel uses the parameters as vectors, in a comparison and in an
`OpSelect`, and the host checks the values every thread sees.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/narrow-builtins.ll` is hand-written AIR, and
`narrow-builtins.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/narrow-builtins.ll -o narrow-builtins.air
    ../air-ops/make-metallib.py narrow-builtins.air narrow-builtins.metallib kernel:narrow_builtins

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
