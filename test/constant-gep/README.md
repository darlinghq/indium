The fixture authors LLVM constant GEP expressions through LLVM-C. Nonzero indices must remain constant expressions; zero is an existing registered-pointer identity control. The global is explicitly associated with a known physical pointer. This does not implement global allocation or identify an original application's expression.

From the repository root:

```sh
flock /tmp/agent-locks/darling-heavy-build.lock c++ -std=c++17 -DHOST_LLVM_LIBNAME='"libLLVM.so"' -Iinclude -Iprivate-include test/constant-gep/constant-gep.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -ldl -lLLVM-22 -o /tmp/constant-gep
git fetch https://github.com/VibeDarling/indium.git a5e8f648127a0d4251eaf95432bd68193d757cc8
git show FETCH_HEAD:test/gep-index/gep-readback.cpp > /tmp/authored-gep-readback.cpp
flock /tmp/agent-locks/darling-heavy-build.lock c++ -std=c++17 /tmp/authored-gep-readback.cpp -lvulkan -o /tmp/gep-readback
for width in 64; do
  for index in 0 1 -1; do
    /tmp/constant-gep "$width" "$index" "/tmp/constant-gep-$width-$index.spv" || exit
    spirv-val --target-env vulkan1.3 "/tmp/constant-gep-$width-$index.spv" || exit
    expected=22; [ "$index" = 1 ] && expected=33; [ "$index" = -1 ] && expected=11
    VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json /tmp/gep-readback "/tmp/constant-gep-$width-$index.spv" "$expected" || exit
  done
done
/tmp/constant-gep 64 1 /tmp/rejected.spv logical
```

Native readback reuses the authored PR19 harness unchanged (published head a5e8f648127a0d4251eaf95432bd68193d757cc8, https://github.com/VibeDarling/indium/blob/a5e8f648127a0d4251eaf95432bd68193d757cc8/test/gep-index/gep-readback.cpp), with its safe three-float allocation and middle address. Use the appropriate ICD on another machine. Standalone main dynamic64 +/-1 controls validate and remain byte-identical across extraction. Standalone upstream main: both retained64-bit expressions reject before and validate/read33/11 after; zero reads22 and nonphysical rejection passes. With separate PR19 narrow-index prerequisite, all four retained expressions reject with unsupported LLVM value kind 10; zero identity validates and reads22. With that prerequisite, all six modules validate under Vulkan1.3 and native Apple M1 reads22/33/11 for both widths. The nonphysical pointer rejects. Private prerequisite-integrated controls also reject unsupported widths1/128 explicitly and preserve dynamic32/64 +/-1 modules byte-for-byte.

Provenance: existing OSS dynamic GEP lowering and vendored LLVM-C declarations (rung2); published LLVM constant-expression/GEP semantics and LLVM22.1.8 Core.cpp LLVMGetGEPSourceElementType using GEPOperator for both instructions and expressions; entirely authored API inputs/readbacks (rung4). Host LLVM was22.1.8. Public source: https://github.com/llvm/llvm-project/blob/llvmorg-22.1.8/llvm/lib/IR/Core.cpp#L3040 and https://llvm.org/docs/LangRef.html#constant-expressions . The existing fixed64 physical-address model is preserved; other DataLayout index widths, logical pointer conversion, global allocation, and sampler expressions remain outside this change.

32-bit native coverage and widths1/128 rejection require PR19 (https://github.com/VibeDarling/indium/pull/19); its production changes are not included here. Repeat the loop with widths32/64 in that integration. Native readback source is a test-only dependency, fetched at its public immutable revision above.
