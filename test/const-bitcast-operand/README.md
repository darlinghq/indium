# const-bitcast-operand

A compute kernel whose AIR contains a `bitcast` whose operand is a **constant**.

Every other fixture's bitcasts re-type a pointer, so their operand is a value
the translator has already emitted a type for. A constant never gets one:
`declareConstantScalar` and its siblings don't call `setResultType`, so looking
the operand's type up finds nothing. This is the only AIR shape in the corpus
where a `LLVMBitCast`'s source type cannot be looked up, and the only one that
reaches the fallback in that case of `src/iridium/air.cpp`.

Before the guard this fixture's bitcast read `pointerStorageClass` out of an
empty `std::optional`. That is not the loud failure it looks like: `std::optional`'s
`operator*` on an empty optional neither throws nor crashes, it hands back the
zeroed storage, which `SPIRV::Type` reads as `StorageClass::UniformConstant`, a
read-only class. Whether that is visible depends entirely on what the recovered
type was, which is why the same read re-typed a real Output pointer into a
read-only one elsewhere and produced `OpStore ... storage class is read-only`.

The test dispatches one thread, reads the 16-byte destination back, and compares
it against the source, plus a negative control that reverses the source floats on
the host and requires the readback to follow.

## Provenance of the .metallib

Like `test/store-reference`, and for the same reason, the `.metallib` is
assembled by hand rather than compiled: a `bitcast` of a constant is legal LLVM,
but LLVM 22's textual parser will not spell one, so the AIR has to be assembled
outside a `.ll` file. `shadersrc/const-bitcast-operand.ll` is the AIR and
`const-bitcast-operand.metallib` wraps it in the MTLB container
`AIR::Library` parses.

To regenerate the bitcode:

    llvm-as shadersrc/const-bitcast-operand.ll -o constant_bitcast_operand.air

then wrap that bitcode in the MTLB container. See `test/store-reference/README.md`
for the container layout and `src/iridium/air.cpp` for the reader.

Once a real `metal` compiler is available, replace the `.metallib` with its
output and drop the `.ll`; nothing else here should need to change.
