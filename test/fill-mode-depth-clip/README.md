# fill-mode-depth-clip

`setTriangleFillMode:` and `setDepthClipMode:` apply to the draws that follow them: the Lines fill mode draws the
outline of a triangle, the Clamp depth clip mode draws what lies beyond the far plane, both persist across a
pipeline change, and a new encoder starts with Fill and Clip.

`graphics.spv` is mslc's output for `graphics.metal` next to it:

    mslc -o graphics.spv graphics.metal

Skips (prints SKIP, exits 0) when there is no Vulkan device.
