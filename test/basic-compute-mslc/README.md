# basic-compute-mslc

`indium-test-basic-compute-mslc` is `test/basic-compute` with a different
compiler on the front. The shader is the same `shadersrc/add.metal`, the same
16,777,216 elements are dispatched on the device, and the readback is compared
bit for bit against what the Metal source says the result has to be. What
changed is that the SPIR-V came from [mslc](https://github.com/cristim/mslc) and
went in through `Device::newLibrary(const void*, size_t, const LibraryReflection&)`
instead of through Iridium.

## The committed fixtures

`add-mslc.spv` and `add-mslc.reflect.json` are committed rather than produced by
the build. mslc is a separate repository that other people work on at the same
time, and a test that generated its own input would be testing whatever mslc
happened to emit that day instead of a known module. They were produced with:

```sh
mslc --reflect add-mslc.reflect.json -o add-mslc.spv -V ../basic-compute/shadersrc/add.metal
```

against mslc 0.1.0 at `5ecb475`, which reported `spirv-val: PASS`. Regenerating
them is a separate, deliberate step, and the result has to be reviewed: the
reflection format is not settled, and a fixture that moves under the test would
be a change to what the test claims.

## What the test does beyond running a kernel

The reflection is read out of mslc's JSON, not written out by hand. That read
stands in for what `darling-metal` would do with `NSJSONSerialization`: indium
takes the values and has no opinion about the document they came from, so the
code that knows mslc's format belongs in the layer that would do the same job
for real. It reads the keys indium needs and stops on anything else rather than
tolerating a change it would then ignore.

The module bytes and the reflection are released the moment `newLibrary`
returns, before anything is dispatched, because that is the contract: indium
borrows both for the duration of the call. A library that had quietly kept a
pointer to either would go wrong from that point on.

Two negative controls, because a readback that cannot fail is not evidence:

- one result element is made wrong, and the comparison has to report exactly one
  mismatch and none once it is put back;
- the same module is dispatched through a reflection whose `metal_index` values
  are rotated, so the answer lands in a different buffer, and the comparison has
  to notice.

The second control is the one that makes the first pass meaningful: it shows the
`metal_index` values out of mslc's reflection are what route the buffers, rather
than the arithmetic happening to be right.

The test also checks that a reflection indium cannot use is refused with a
reason naming the problem, rather than becoming a descriptor set layout with a
defaulted binding in it.

## What mslc's reflection does not carry

`reflection_version: 1` describes one entry point with buffer bindings, and that
is all it describes today. Concretely, against what `Indium::LibraryReflection`
asks a producer for:

- **Textures.** Nothing in the document is `"kind": "Texture"`. Every entry mslc
  emits is `"kind": "Buffer"`, from the one place it writes reflection
  (`src/sema.cpp`, the parameter loop). A shader that reads a texture has no
  entry for it, so indium would build a descriptor set layout with no image in
  it and the shader's `OpTypeImage` would have nothing bound.
- **Samplers.** No `"kind": "Sampler"` either, so
  `BindingDescriptor::embeddedSamplerIndex` has no source, and
  `FunctionReflection::embeddedSamplers` is empty for every function. A shader
  with an `[[sampler(...)]]` attribute reflects nothing for it.
- **Vertex inputs.** No `BindingType::VertexInput` entry, so a vertex stage's
  attribute bindings are missing in the same way.
- **Per-function structure.** The document has one `entry_point` at the top
  level, not a map of functions. `LibraryReflection::functions` is a map,
  because indium needs to address functions by name, so a multi-entry-point
  module has to be described as several documents or the format has to grow.
- **Local size.** `local_size` is present, and indium does not need it from the
  reflection: it comes from the module, which carries `OpExecutionModeId
  LocalSizeId` over `SpecId` 0, 1 and 2, and indium fills those from the
  threadgroup size given to `dispatchThreads`. It is worth a producer keeping it
  in step with the module, but indium does not read it.
- **Strict JSON.** The document has a trailing comma after the last binding,
  which strict parsers reject, `NSJSONSerialization` among them. The reader in
  the test does not care; a strict one would. `cristim/mslc` PR #33 is where
  that decision is being made, and it is not this repository's to make.

So: buffers work end to end today, and everything else is a gap in the producer
rather than in indium's API. The API asks for all of it, and refuses a
reflection that leaves a stage unset rather than guessing one.
