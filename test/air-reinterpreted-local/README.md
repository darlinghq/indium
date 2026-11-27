# air-reinterpreted-local

Two compute kernels whose AIR loads and stores a local object through a pointer of another type, and a kernel
argument whose C++ type is a `class`:

- `reinterpret_load` builds a local struct of four floats and reads it back as one `float4`, and takes a
  `device Counters*` argument where `Counters` is declared with `class`, so that its LLVM type is named
  `class.Counters` rather than `struct.Counters`;
- `reinterpret_store` stores a `float4` into a local struct of four floats and copies the fields out.

A local variable has no address to re-type in SPIR-V, so Iridium has to read or write the scalar pieces of the
declared type instead, and a class-named buffer type has to resolve like a struct-named one. The test dispatches
eight threads, compares every result float with what `shadersrc/air-reinterpreted-local.metal` says the kernels
compute, and checks that the store to the class-typed buffer arrived.

## Provenance of the .metallib

`shadersrc/air-reinterpreted-local.ll` is the AIR that Apple's `metal` (Xcode 26, macOS 26.7.3) produced with `-O2`
for `shadersrc/air-reinterpreted-local.metal`, with the bodies of the two kernels edited by hand: `-O2` replaces
the local struct by registers, so the compiler's own output never loads or stores it through a pointer of another
type. The edit puts the struct back as an `alloca` that is written with four scalar stores and read with one
`<4 x float>` load (`reinterpret_load`), and written with one `<4 x float>` store and read back with four scalar
loads (`reinterpret_store`). The `Counters` argument and its metadata are the compiler's.

    llvm-as shadersrc/air-reinterpreted-local.ll -o air-reinterpreted-local.air
    ./make-metallib.py air-reinterpreted-local.air air-reinterpreted-local.metallib kernel:reinterpret_load kernel:reinterpret_store

with `make-metallib.py` of `test/air-ops`.
