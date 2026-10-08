These fixtures author LLVM constants through the LLVM C API and SPIR-V through our builder. They do not load compiled shaders. Integer cases cover zero, minus one, minimum and maximum signed values for 8/16/32/64 bits; boolean cases cover false/true. Typed arithmetic and branches expose mismatched result types. Unsupported 3/128-bit constants must fail before LLVM value extraction.

```sh
c++ -std=c++17 -DHOST_LLVM_LIBNAME='"libLLVM.so"' -Iinclude -Iprivate-include test/numeric-constants/numeric-constants.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -ldl -lLLVM-22 -o /tmp/numeric-constants
/tmp/numeric-constants /tmp/authored-numeric
c++ -std=c++17 -Iinclude -Iprivate-include test/numeric-constants/boolean-specialization.cpp src/iridium/spirv.cpp -o /tmp/boolean-specialization
/tmp/boolean-specialization /tmp/authored-boolean.spv
for module in /tmp/authored-numeric-*.spv /tmp/authored-boolean.spv; do spirv-val --target-env vulkan1.2 "$module" || exit; done
```

Adjust the LLVM link library to the installed version. The baseline validates only eight 32/64-bit modules; ten boolean/narrow modules fail. It also fails both unsupported-width checks. The candidate preserves all 18 bit patterns, rejects both unsupported widths, validates all 18 modules and the additional boolean-specialization module. The latter checks ordinary/specialized identity and boolean instruction lengths.
