# nonuniform-dispatch

`dispatchThreads:threadsPerThreadgroup:` over a grid that is not a multiple of the threadgroup size. A kernel
that mslc compiled with `--mask-grid-size` skips the threads beyond the grid, so exactly the threads of the grid
run, in one, two and three dimensions. A kernel with a barrier is not masked and the dispatch is refused.

`grid.spv` and `barrier.spv` are mslc's output for the `.metal` files next to them:

    mslc --mask-grid-size -o grid.spv grid.metal
    mslc --mask-grid-size -o barrier.spv barrier.metal

Skips (prints SKIP, exits 0) when there is no Vulkan device.
