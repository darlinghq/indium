# viewport-origin

Renders a solid rectangle into a 32x24 offscreen texture and checks every pixel
against an explicitly specified native Vulkan reference rectangle. Exercises `setViewport` and
both `setViewports` overloads, with zero, positive, fractional and negative
origins, and a rectangle reaching the attachment's bottom-right edge.

The native reference bypasses Indium's viewport conversion. Its integer cases
are also checked against the exact top-left-origin rectangle. For fractional
edges, the reference captures the selected driver's rasterization, and every
pixel from each Indium API must match it without tolerance or skipped edges.

The test is based on the public
[MTLViewport contract](https://developer.apple.com/documentation/metal/mtlviewport).
The Vulkan backend negates viewport height; the corresponding `y` value must
locate the lower edge, as described by
[VkViewport](https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html).

`viewport.spv` is generated from the independently authored `viewport.metal`:

```sh
mslc -o viewport.spv --validate viewport.metal
spirv-val --target-env vulkan1.2 viewport.spv
```

The fixture uses mslc 0.1.0; as with the other mslc-based tests, shader generation
is separate from the test build. No Apple shader binary is used.

```sh
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target indium-test-viewport-origin --parallel 4
ctest --test-dir build -R '^viewport-origin$' --output-on-failure
```

No window system is required. Without a Vulkan device, exits 77 (CTest skip).
