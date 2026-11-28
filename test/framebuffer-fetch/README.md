# framebuffer-fetch

Framebuffer fetch (`[[color(n)]]` in MSL): a fragment function reads the color attachment `n` of its render pass.
`fetch.spv` is `shadersrc/framebuffer-fetch.metal` compiled by mslc (darling-metal's `deps/mslc`, native build of
its `mslc-cli`), and `handwritten.spv` is `shadersrc/handwritten.spvasm`, two functions written by hand in SPIR-V.
Both are committed rather than produced by the build:

```sh
mslc -V --reflect fetch.json -o fetch.spv shadersrc/framebuffer-fetch.metal
spirv-as --target-env spv1.5 shadersrc/handwritten.spvasm -o handwritten.spv
spirv-val --target-env vulkan1.2 handwritten.spv
```

The reflection mslc writes (`fetch.json`) says, for `invert_fs`,
`{ "kind": "InputAttachment", "metal_index": 0, "descriptor": { "set": 1, "binding": 0 }, "input_attachment_index": 0 }`;
the test builds the `LibraryReflection` that reading it gives: `BindingType::InputAttachment`, `index` = the
color attachment (`metal_index`), `internalIndex` = the descriptor binding. Reading mslc's JSON is darling-metal's
job, not indium's.
