The fixture authors a float buffer GEP and load through LLVM C API. Its optional `compute` mode stores the loaded value to a second buffer. No guest shader input is used.

```sh
c++ -std=c++17 -DHOST_LLVM_LIBNAME='"libLLVM.so"' -Iinclude -Iprivate-include test/gep-index/gep-index.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -ldl -lLLVM-22 -o /tmp/gep-index
c++ -std=c++17 test/gep-index/gep-readback.cpp -lvulkan -o /tmp/gep-readback
for width in 32 64; do
  for index in 1 -1; do
    /tmp/gep-index "$width" "$index" "/tmp/authored-gep-$width-$index.spv" compute || exit
    spirv-val --target-env vulkan1.3 "/tmp/authored-gep-$width-$index.spv" || exit
    expected=33; [ "$index" = -1 ] && expected=11
    VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json /tmp/gep-readback "/tmp/authored-gep-$width-$index.spv" "$expected" || exit
  done
done
```

Use the appropriate ICD on another machine. The harness logs the physical device and explicitly requires shaderInt64, bufferDeviceAddress and maintenance4. It reads only safe offsets: the source buffer contains 11,22,33, and the passed address points to its middle element. +1 reads 33; -1 reads 11. Host-coherent mapped output is read after a compute-to-host memory barrier and queue completion.

Before the fix, both 32-bit index modules produce successfully but fail validation at pointer offset multiplication. Both 64-bit modules validate and native readback returns 33/11. After the fix all four validate and return those values on Apple M1. Vertex load/FAdd mode also validates with Vulkan1.2. Widths8/16 have positive/negative validator controls; native readback deliberately covers only32/64 because narrower shader features are not enabled by this harness. Widths1/128 are explicitly rejected.

Provenance: existing fixed PhysicalStorageBuffer64 lowering and https://llvm.org/docs/LangRef.html#getelementptr-instruction require signed extension of a narrower first index before offset multiplication. This change supports the translator's existing 64-bit physical address model. Different DataLayout pointer-index widths, wider integer indices, and allocation-size/layout corrections remain outside this concern.
