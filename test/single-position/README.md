# Explicit single position output

The LLVM-C-API fixture authors every input module. It exercises the existing
`air.position` return metadata, a struct control, ordinary float4 output, and
invalid scalar/stage declarations. No application shader is decoded or inspected.

From the repository root (LLVM22 and Vulkan development packages required):
```sh
c++ -std=c++17 '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include test/single-position/position-module.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -ldl -lLLVM-22 -o /tmp/position-module
c++ -std=c++20 test/single-position/position-render.cpp -lvulkan -o /tmp/position-render
for mode in single struct fragment ordinary; do
  /tmp/position-module "$mode" "/tmp/position-$mode.spv"
  spirv-val --target-env vulkan1.3 "/tmp/position-$mode.spv"
done
/tmp/position-module invalid-type /tmp/invalid.spv # must exit1
/tmp/position-module invalid-stage /tmp/invalid.spv # must exit1
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json /tmp/position-render /tmp/position-single.spv /tmp/position-fragment.spv
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json /tmp/position-render /tmp/position-struct.spv /tmp/position-fragment.spv
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json /tmp/position-render /tmp/position-ordinary.spv /tmp/position-fragment.spv # must exit1
```

Select an appropriate native ICD on other systems. The renderer requires Vulkan1.3
and bufferDeviceAddress, submits a fullscreen triangle and reads 300x200 exact RGBA
pixels. Before the fix, single output validates but yields 0 red pixels/exit1; struct yields
60000 red pixels/exit0. After, both yield60000 red pixels/exit0 on Apple M1. Ordinary output was observed as 0 red pixels on M1; an unwritten Position has
unspecified rendering. Float4 alone does not establish the Position semantic. Fragment output
continues to produce the red color; malformed position declarations reject.
