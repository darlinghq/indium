# texture-fetch

A kernel that only reads its textures (`access::read`, `TextureAccessType::Fetch`) reads them by integer
coordinate at a level chosen at run time. Indium binds such a texture as a sampled image of all its levels, as a
Vulkan storage image view has one level, and a cube as the 2D array of its faces, as an image fetch takes no
cube image.

`fetch.spv` is mslc's output for `fetch.metal` next to it:

    mslc -o fetch.spv fetch.metal

Skips (prints SKIP, exits 0) when there is no Vulkan device.
