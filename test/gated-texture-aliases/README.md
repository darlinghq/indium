# gated-texture-aliases

`gated-texture-aliases.spv` and `gated-texture-aliases.reflect.json` are committed rather than produced by the build, as
in `basic-compute-mslc`. They were produced from `shadersrc/gated-texture-aliases.metal` with:

```sh
mslc -V --reflect gated-texture-aliases.reflect.json -o gated-texture-aliases.spv shadersrc/gated-texture-aliases.metal
```

against mslc at `d21275d`, which reported `spirv-val: PASS`. The test writes out the reflection by hand from the JSON.

The four source arguments of `read_source` take `texture(0)`, each gated by a function constant derived from
`kSourceType` (as ANGLE's blit shaders gate theirs), and mslc gives each its own binding, 1 to 4.
