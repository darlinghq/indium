# indirect-draw-dispatch

Indirect dispatches and draws, where the GPU reads the threadgroup count or the draw arguments from a buffer:
host-written and GPU-written arguments, a non-zero argument offset, zero counts, the first vertex and first index,
and arguments that are misaligned or outside the buffer (they are not executed).

`compute.spv` and `graphics.spv` are mslc's output for the `.metal` files next to them:

    mslc -o compute.spv compute.metal
    mslc -o graphics.spv graphics.metal

Skips (prints SKIP, exits 0) when there is no Vulkan device.
