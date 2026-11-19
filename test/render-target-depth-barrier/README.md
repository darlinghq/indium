# render-target-depth-barrier

Metal's render-target barrier scope covers the depth attachment as well as the colour ones. A fragment shader that
reads the depth written by earlier draws of the same pass, after a `memoryBarrier` with the render-target scope, must
see that depth. The render pass has to admit the depth stages and access in its self-dependency for the barrier to be
legal inside it, and the barrier has to cover them to order the depth write before the read.

The test writes depth 0.5 with one draw, records the barrier, and reads the depth back as a texture in a second draw
of the same pass. It checks the pixels, which the Vulkan implementations here compute correctly either way; the point
is the Khronos validation layer with synchronization validation enabled, which reports a read-after-write hazard on
the depth image (`VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`) when the barrier does
not cover depth.

`graphics.spv` is mslc's output for `graphics.metal`:

    mslc -o graphics.spv graphics.metal

Skips (prints SKIP, exits 0) when there is no Vulkan device.
