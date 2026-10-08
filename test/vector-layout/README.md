# Ordinary vector metadata alignment

This authored LLVM-C fixture compares ordinary Metal vector names against the
existing LLVM vector mapper (clean-room rung 2: `src/iridium/air.cpp`). It then
loads the first component of the second padded float3 in a two-element array
(rung 4: authored inputs and native Vulkan readback).

Before the fix, ordinary vector type checks fail 15/18. Declaring float3 metadata
before the array emits stride12 and fails Vulkan validation; declaring a scalar
first validates and reads44. After the fix, both orders validate and read44 on
Apple M1. Packed names retain their previous layout, checked separately; this
change does not establish correct packed-vector support.

From this checkout, compile each producer serially under the shared lock:

```sh
flock /tmp/agent-locks/darling-heavy-build.lock c++ -std=c++17 \
  '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include \
  test/vector-layout/vector-types.cpp src/iridium/spirv.cpp \
  src/iridium/dynamic-llvm.cpp -ldl -lLLVM-22 -o /tmp/vector-types
/tmp/vector-types
# Repeat the compile with vector-layout.cpp and output /tmp/vector-layout.
/tmp/vector-layout vector-first /tmp/vector-first.spv
/tmp/vector-layout scalar-first /tmp/scalar-first.spv
spirv-val --target-env vulkan1.2 /tmp/vector-first.spv
spirv-val --target-env vulkan1.2 /tmp/scalar-first.spv
flock /tmp/agent-locks/darling-heavy-build.lock c++ -std=c++17 \
  test/gep-index/gep-readback.cpp -lvulkan -o /tmp/vector-readback
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json \
  /tmp/vector-readback /tmp/vector-first.spv 44 vector-array
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json \
  /tmp/vector-readback /tmp/scalar-first.spv 44 vector-array
```

Full array-module verification requires the aligned-array-stride fix in PR10
and the GEP translation fixes in PR19. The standalone type checks reproduce
and verify this fix on main without those prerequisites. The shared readback
harness comes from the GEP-index regression (PR19); its original
three-argument scalar mode still reads33/11 for signed32/64-bit indices +1/-1.
These fixtures demonstrate the translator defect, not complete app rendering.
