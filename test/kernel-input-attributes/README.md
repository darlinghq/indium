# kernel-input-attributes

Compute kernels whose parameters are the kernel input attributes of the Metal Shading Language that describe
quad-groups (`thread_index_in_quadgroup`, `quadgroup_index_in_threadgroup`, `quadgroups_per_threadgroup`,
`dispatch_quadgroups_per_threadgroup`) and the threadgroup size given at dispatch
(`dispatch_threads_per_threadgroup`, `dispatch_simdgroups_per_threadgroup`), as 32-bit and 16-bit integers. They run over threadgroups of 64 and of 7x3 threads, and the host compares every
thread's values with what the specification defines.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/kernel-input-attributes.ll` is hand-written AIR, and
`kernel-input-attributes.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/kernel-input-attributes.ll -o kernel-input-attributes.air
    ../air-ops/make-metallib.py kernel-input-attributes.air kernel-input-attributes.metallib kernel:quad_ids kernel:quad_ids_short kernel:grid_sizes kernel:grid_sizes_short

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
