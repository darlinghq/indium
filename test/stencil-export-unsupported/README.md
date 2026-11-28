# stencil-export-unsupported

`stencil-export-unsupported.spv` and `stencil-export-unsupported.reflect.json` are committed rather than produced by the
build, as in `basic-compute-mslc`. They were produced from `shadersrc/stencil-export-unsupported.metal` with:

```sh
mslc -V --reflect stencil-export-unsupported.reflect.json -o stencil-export-unsupported.spv shadersrc/stencil-export-unsupported.metal
```

against mslc at `d21275d`, which reported `spirv-val: PASS`. The functions have no resources, so the test's reflection
lists only their names and stages.

The library has one vertex function and two fragment functions; `stencil_fs` writes the stencil reference
(`[[stencil]]`), so the module declares the `StencilExportEXT` capability, and `color_fs` does not.

`late-private-pointer.spv` and `copied-pointer.spv` are assembled from the `.spvasm` files of the same name in
`shadersrc` with:

```sh
spirv-as --target-env spv1.5 shadersrc/late-private-pointer.spvasm -o late-private-pointer.spv
spirv-as --target-env spv1.5 shadersrc/copied-pointer.spvasm -o copied-pointer.spv
```

and both pass `spirv-val --target-env vulkan1.2`. The first declares a private pointer to the stencil reference's type
only after the stencil reference variable, and decorates the variable Invariant; the second writes the stencil
reference through a copy of the variable's pointer, which the rewrite cannot make private.
