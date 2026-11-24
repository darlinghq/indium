# barrier-semantics

Compute kernels with threadgroup and simdgroup barriers (`threadgroup_barrier`, `simdgroup_barrier`) and fences
(`atomic_thread_fence`) that order no memory (`mem_none`), only imageblock memory, which Vulkan doesn't have,
or device and threadgroup memory, and relaxed fences. Each thread stores around them and the host checks that
every store happened; the translation is also validated with `spirv-val`.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/barrier-semantics.ll` is hand-written AIR, and
`barrier-semantics.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/barrier-semantics.ll -o barrier-semantics.air
    ../air-ops/make-metallib.py barrier-semantics.air barrier-semantics.metallib kernel:barriers kernel:fences

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
