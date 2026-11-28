# quad-functions

Compute kernels that call the quad-group functions of the Metal Shading Language: `quad_shuffle`,
`quad_shuffle_xor`, `quad_shuffle_down`, `quad_shuffle_up`, `quad_shuffle_and_fill_down` and
`quad_shuffle_and_fill_up` of 32-bit integers, floats, 8-bit integers and halfs, and `quad_ballot`, also with
only some lanes active; and the votes of masks, `simd_vote::all()` and `any()` and their quad-group
counterparts, of ballots and of masks with bits set past the lanes of the group. The host compares every
thread's results with what the specification defines, where a quad-group is four consecutive lanes of a
simdgroup.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/quad-functions.ll` is hand-written AIR, and
`quad-functions.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/quad-functions.ll -o quad-functions.air
    ../air-ops/make-metallib.py quad-functions.air quad-functions.metallib kernel:quad_shuffles kernel:quad_shuffles_float kernel:quad_shuffles_char kernel:quad_shuffles_half kernel:quad_ballot kernel:votes

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
