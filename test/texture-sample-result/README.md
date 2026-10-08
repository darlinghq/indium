The fixture authors a complete LLVM module through the C API, with a texture/sampler parameter pair described using the existing AIR metadata parser's names. It calls the existing sampling handler with an explicitly authored `{float4, status}` or `{half4, status}` result. No compiled guest shader input is used.

```sh
c++ -std=c++17 -DHOST_LLVM_LIBNAME='"libLLVM.so"' -Iinclude -Iprivate-include test/texture-sample-result/texture-sample-result.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -ldl -lLLVM-22 -o /tmp/texture-sample-result
for width in 1 8 16 32 64; do
  /tmp/texture-sample-result "$width" "/tmp/authored-sample-float-$width.spv" || exit
  spirv-val --target-env vulkan1.2 "/tmp/authored-sample-float-$width.spv" || exit
  /tmp/texture-sample-result "$width" "/tmp/authored-sample-half-$width.spv" half || exit
  spirv-val --target-env vulkan1.2 "/tmp/authored-sample-half-$width.spv" || exit
done
/tmp/texture-sample-result 0 /tmp/unsupported-sample-float.spv
/tmp/texture-sample-result 0 /tmp/unsupported-sample-half.spv half
```

The final two commands must reject floating-point status members and exit 1. Adjust the LLVM link library for the installed version. Baseline: only both i8 cases validate; eight boolean/other integer cases fail composite insertion type validation, and unsupported floating status is emitted invalidly. Candidate: all ten supported cases validate; both unsupported cases fail explicitly.

Specification: existing sampling handler's two-member result/zero-status convention; Khronos SPIR-V `OpCompositeInsert` requires the inserted type to match the selected member, and `OpConstantNull` initializes composite members recursively according to their declared types. This fix preserves that existing zero-status behavior and does not establish new sampler-state ABI semantics.
