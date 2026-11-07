# Select the requested entry's metadata

All LLVM modules are authored by this fixture. They contain unrelated stage
metadata or a same-stage descriptor before the requested function. The existing
metadata contract stores the LLVM function as root operand0; match that function
instead of choosing the first stage present and descriptor0 (clean-room rung2,
`Function::analyze` in `src/iridium/air.cpp`; rung4, authored API inputs).

Compile serially under the shared build lock:

```sh
flock /tmp/agent-locks/darling-heavy-build.lock c++ -std=c++17 \
  '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include \
  test/entry-metadata/entry-module.cpp src/iridium/spirv.cpp \
  src/iridium/dynamic-llvm.cpp -ldl -lLLVM-22 -o /tmp/entry-module
/tmp/entry-module single /tmp/entry-single.spv
/tmp/entry-module fragment /tmp/entry-fragment.spv
/tmp/entry-module second /tmp/entry-second.spv
spirv-val --target-env vulkan1.3 /tmp/entry-single.spv
spirv-val --target-env vulkan1.3 /tmp/entry-fragment.spv
spirv-val --target-env vulkan1.3 /tmp/entry-second.spv
/tmp/entry-module missing /tmp/unused.spv # must exit1
/tmp/entry-module duplicate /tmp/unused.spv # must exit1
/tmp/entry-module malformed /tmp/unused.spv # must exit1
```

Before the fix, single passes, fragment exits1 with wrong stage, and second exits1
with an unmapped argument. After, all three valid cases pass and validate; the
three invalid cases reject explicitly. Invalid baseline modules are not submitted
to the GPU.

Native readback reuses the position renderer from PR21 (which also supplies
correct single Position output; required for this additional pixel check):

```sh
flock /tmp/agent-locks/darling-heavy-build.lock c++ -std=c++20 \
  test/single-position/position-render.cpp -lvulkan -o /tmp/position-render
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json \
  /tmp/position-render /tmp/entry-single.spv /tmp/entry-fragment.spv
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json \
  /tmp/position-render /tmp/entry-second.spv /tmp/entry-fragment.spv
```

Both candidate pairs read60000/60000 red pixels on Apple M1. This verifies the
translator's authored mixed-entry scenario, not the remaining actual app failure.
