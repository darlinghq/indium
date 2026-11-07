# air-ops

Compute kernels covering the AIR instructions and Metal library functions that have a direct SPIR-V
counterpart: integer arithmetic of both signednesses, shifts, bitwise operations, comparisons,
`select`, integer and floating-point conversions, `frem`, `fneg`, the math functions, and a load
from a program-scope constant array. `control_flow` adds loops (counted, nested, and one with two
exits) and an if/else, which have to come out as SPIR-V structured control flow. Each thread computes every operation on its own inputs and
the host compares the results with what the Metal Shading Language specification defines.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/air-ops.ll` is hand-written AIR for
`shadersrc/air-ops.metal`, and `air-ops.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/air-ops.ll -o air-ops.air
    ./make-metallib.py air-ops.air air-ops.metallib kernel:int_ops kernel:float_ops kernel:table_lookup kernel:control_flow

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
