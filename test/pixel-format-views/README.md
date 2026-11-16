# pixel-format-views

Texture pixel formats that Vulkan does not have one-to-one, and views of textures as other formats:

- A8Unorm: stored as R8, read as (0, 0, 0, a), upload and readback by the byte, no shader writes
- X32_Stencil8 / X24_Stencil8: stencil-only views of the combined depth/stencil formats
- views of the same texel size, such as RGBA8Unorm as RGBA8Unorm_sRGB, which need
  MTLTextureUsagePixelFormatView on the texture, and the formats that have no view

The kernel reads one texel of a float texture with `read` (a texel fetch, so the format conversion and the
swizzle are what is tested, not a sampler). `read-float.spv` is mslc's output for `read-float.metal`:

    mslc -o read-float.spv read-float.metal

The stencil and R32Uint views are read by copying them into a buffer with the blit encoder: mslc cannot yet
lower a `texture2d<uint>` that is sampled rather than `access::read`, and a read-access texture is a storage
image, which a stencil view and a texture without ShaderWrite usage cannot be.

Skips (prints SKIP, exits 0) when there is no Vulkan device.
