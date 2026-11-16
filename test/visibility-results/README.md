# visibility-results

`RenderPassDescriptor::visibilityResultBuffer` and `RenderCommandEncoder::setVisibilityResultMode()`: the samples
the draws after the call pass (depth test, discard, scissor) are counted into the 8-byte slot at the offset, as a
count (Counting) or as 0 or 1 (Boolean), summed when one slot is used again within an encoder, written when the
command buffer completes, and left alone for slots no call named and for the Disabled mode.

`graphics.spv` is mslc's output for `graphics.metal` next to it:

    mslc -o graphics.spv graphics.metal

Skips (prints SKIP, exits 0) when there is no Vulkan device. A device without hostQueryReset ignores the calls, so
the test fails there.
