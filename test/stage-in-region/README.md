# stage-in-region

Compute kernels whose parameters are the stage-in region, the Metal Shading Language's `[[grid_origin]]` and
`[[grid_size]]` (AIR's `stage_in_grid_origin` and `stage_in_grid_size`), as `uint3`, `uint2` and `uint`. The
host dispatches each without a region, after `setStageInRegion()`, again in the same encoder, after another
`setStageInRegion()`, and in a new encoder, and checks that the kernels read the region that was set, and an
empty region at the origin (all zeros) where none was.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/stage-in-region.ll` is hand-written AIR, and
`stage-in-region.metallib` wraps it in the MTLB container Iridium reads:

    llvm-as shadersrc/stage-in-region.ll -o stage-in-region.air
    ../air-ops/make-metallib.py stage-in-region.air stage-in-region.metallib kernel:region kernel:region_2d kernel:region_1d

Assemble it with the oldest LLVM Iridium supports (LLVM 19), since bitcode written by a newer LLVM
may not be readable by an older one.
