The fixture parses only its own explicitly authored minimal pointer identity function through LLVM's C API, then authors metadata and a texture-sampling consumer through that API. LLVMBuildBitCast folds equal opaque pointer types, so the authored parser input is necessary to retain the identity instruction for this regression. No compiled guest shader input is used.

```sh
c++ -std=c++17 -DHOST_LLVM_LIBNAME='"libLLVM.so"' -Iinclude -Iprivate-include test/opaque-pointer-identity/opaque-pointer-identity.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -ldl -lLLVM-22 -o /tmp/opaque-pointer-identity
/tmp/opaque-pointer-identity /tmp/authored-pointer.spv
spirv-val --target-env vulkan1.2 /tmp/authored-pointer.spv
```

Adjust the LLVM library version if needed. Baseline emits a logical pointer bitcast whose subsequent load fails validation (exit 1). Candidate associates the identity instruction with the original typed resource result; the sampled consumer validates (exit 0). Numeric scalar/vector destination tests also validate.

Provenance: existing opaque pointer recovery supplies no changed destination pointee; LLVM's published pointer bitcast contract preserves address space and value, while Khronos SPIR-V OpBitcast requires a different destination type and restricts logical pointer operands. Reuse the known identity instead of emitting an invalid equal-type logical pointer operation.
