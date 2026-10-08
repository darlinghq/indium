`physical-gep.cpp` authors a compute kernel with LLVM's C API: it loads from a
`float4x4` buffer through a `getelementptr` whose source element type is not the
buffer's declared pointee, and stores the result. Shapes: `vector` (`<4 x float>`
source, index 1), `scalar` (`float` source, index 5) and `array` (`[4 x <4 x float>]`
source, i64 indices 0 and 1, the declared layout, as a control) and `array32` (the same with i32 indices). Nothing here comes
from third-party compiler output.

From the repository root (`$OUT` is a scratch directory):

```sh
c++ -std=c++17 -ffunction-sections -fdata-sections '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include test/physical-gep/physical-gep.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -Wl,--gc-sections -lLLVM -ldl -o "$OUT/physical-gep"
c++ -std=c++17 test/physical-gep/readback.cpp -lvulkan -o "$OUT/readback"
for shape in vector scalar array array32; do
  "$OUT/physical-gep" $shape "$OUT/$shape.spv" && spirv-val --target-env vulkan1.3 --relax-block-layout --scalar-block-layout "$OUT/$shape.spv"
done
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/vector.spv" 4 5 6 7
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/scalar.spv" 5
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/array.spv" 4 5 6 7
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/array32.spv" 4 5 6 7
```

Before the change `vector` and `scalar` translate but `spirv-val` rejects them
(`OpAccessChain` result type does not match the type indexed from the base); `array` and `array32`
validate. After the change all three validate and the Apple M1 readback returns
the expected values (the input buffer holds 0..15).
