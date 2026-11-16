# buffer-backed-texture

A texture made from the rows of a buffer with `Buffer::newTexture()`: the buffer's bytes are the texture's pixels
and the other way round, at the offset and row pitch the texture was made with. A texture that does not fit, has
a row pitch the device does not use, or is not a plain 2D texture of an uncompressed format is refused.

`read-float.spv` is mslc's output for `read-float.metal` next to it:

    mslc -o read-float.spv read-float.metal

Skips (prints SKIP, exits 0) when there is no Vulkan device or the device has no linear textures of the format.
