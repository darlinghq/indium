# store-reference

A compute kernel that stores through a re-typed pointer, the mirror of the loads
`test/lighting`'s vertex stage performs. It is the only fixture in the corpus
whose SPIR-V exercises the store path against a pointer whose declared pointee
is not what is being written.

Every store here is either through a pointer that names an aggregate rather than
the field it lands on, or through one that names the `float3` column rather than
the 16 bytes a Metal column is padded to. With AIR's opaque pointers nothing in
the pointer records the intended pointee, so `spirv-val` rejects the module
unless the translator re-types the pointer at the point of use.

The test dispatches one thread, reads the 64-byte destination buffer back, and
compares every byte against what `shadersrc/store-reference.metal` says the
shader should have written, plus a negative control that permutes the source on
the host and requires the readback to follow.

## Provenance of the .metallib

Every other fixture's `.metallib` comes out of Apple's `metal` compiler. This
one was assembled by hand, because no Metal compiler was available where this
fixture was written: `shadersrc/store-reference.ll` is the AIR, written in the
shape `metalfe` emits (the `struct.metal::matrix` column idiom is copied from
`test/lighting`'s `vertex_project`, where it appears on the load side), and
`store-reference.metallib` wraps it in the MTLB container `AIR::Library` parses.

To regenerate it:

    llvm-as shadersrc/store-reference.ll -o write_columns.air

then wrap that bitcode in the MTLB container `AIR::Library` parses: a 0x58-byte
`MTLB` header (u64 fileSize, funcListOffset, funcListSize, pub/privMetaOffset and
Size, bcOffset and bcSize, read from 0x10 on), a function list of a u32 count
followed by tag groups of `NAME`/`TYPE`/`MDSZ`/`OFFT` tags each prefixed with
their 4-char name and a u16 payload size, a u32 group size that counts itself,
and an `ENDT` tag; then the pub/priv metadata blocks, and finally the bitcode
region `OFFT` points into, wrapped in an LLVM `BitcodeWrapperHeader` (magic
0x0b17c0de). See `src/iridium/air.cpp` for the reader.

Once a real `metal` compiler is available, replace the `.metallib` with its
output and drop the `.ll`; nothing else here should need to change.
