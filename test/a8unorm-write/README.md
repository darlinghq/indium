# a8unorm-write

An A8Unorm texture is written by shaders and rendered to like any other format. It has only an alpha channel,
so the alpha that a kernel writes (`float4(r, g, b, a)`) or a fragment returns is the stored byte, a render
target clears to the alpha of the clear colour, blends with the alpha factors and is written when the colour
write mask has its alpha bit.

`a8.spv` is mslc's output for `a8.metal` next to it:

    mslc -o a8.spv a8.metal

On a device without VK_FORMAT_A8_UNORM_KHR (VK_KHR_maintenance5) A8Unorm is stored as R8 and read through a
swizzle; shader writes and render targets are refused there, and the test checks only that.

Skips (prints SKIP, exits 0) when there is no Vulkan device.
