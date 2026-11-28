# float-controls-library

mslc puts the SignedZeroInfNanPreserve execution mode on its entry points (32-bit on every one, 16-bit and 64-bit
where the module uses those widths). Vulkan allows a pipeline of such an entry point only where the device reports
shaderSignedZeroInfNanPreserveFloat16, ...32 or ...64 for the width (VUID-RuntimeSpirv-shaderSignedZeroInfNanPreserveFloat16-06293,
-Float32-06294, -Float64-06295). indium refuses the library, at `newLibrary(spirv, ...)`, with `nullptr` and a message,
instead of failing later at pipeline creation. The test checks that, on devices that report none, some or all of the
widths (the property query is replaced while a device is made, as no test machine lacks the widths), that the devices
are still listed, and that a library translated from AIR is made whatever the device reports.

The modules are assembled from `shadersrc` with

```sh
spirv-as --target-env spv1.5 shadersrc/float32.spvasm -o float32.spv
spirv-as --target-env spv1.5 shadersrc/float16-32-64.spvasm -o float16-32-64.spv
spirv-as --target-env spv1.5 shadersrc/plain.spvasm -o plain.spv
```

and all three pass `spirv-val --target-env vulkan1.2`.
