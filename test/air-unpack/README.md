# air-unpack

A compute kernel that unpacks known 32-bit values with the AIR forms of MSL's `unpack_unorm4x8_to_float`,
`unpack_snorm4x8_to_float`, `unpack_unorm2x16_to_float`, `unpack_snorm2x16_to_float` and
`unpack_unorm4x8_srgb_to_float` (`air.unpack.<format>[.srgb].<type>`). The host compares every channel with
the conversion formulas of the Metal Shading Language specification (section on conversion between packed
formats and floating point).

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/air-unpack.ll` is hand-written AIR for `shadersrc/air-unpack.metal`:

    llvm-as-19 shadersrc/air-unpack.ll -o air-unpack.air
    ../air-ops/make-metallib.py air-unpack.air air-unpack.metallib kernel:unpack
