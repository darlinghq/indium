`physical-gep.cpp` authors a compute kernel with LLVM's C API: it loads from a
`float4x4` buffer through a `getelementptr` whose source element type is not the
buffer's declared pointee, and stores the result. Shapes: `vector` (`<4 x float>`
source, index 1), `scalar` (`float` source, index 5) and `array` (`[4 x <4 x float>]`
source, i64 indices 0 and 1, the declared layout, as a control), `array32` (the same with i32 indices)
and `vec3` (`<3 x float>` source, index 1). Nothing here comes from third-party compiler output.

From the repository root (`$OUT` is a scratch directory):

```sh
c++ -std=c++17 -ffunction-sections -fdata-sections '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include test/physical-gep/physical-gep.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -Wl,--gc-sections -lLLVM -ldl -o "$OUT/physical-gep"
c++ -std=c++17 test/physical-gep/readback.cpp -lvulkan -o "$OUT/readback"
for shape in vector scalar array array32 vec3; do
  "$OUT/physical-gep" $shape "$OUT/$shape.spv" && spirv-val --target-env vulkan1.3 --relax-block-layout --scalar-block-layout "$OUT/$shape.spv"
done
spirv-dis "$OUT/vec3.spv" | grep 'OpIMul %ulong %long_1 %ulong_16'
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/vector.spv" 4 5 6 7
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/scalar.spv" 5
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/array.spv" 4 5 6 7
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/array32.spv" 4 5 6 7
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/vec3.spv" 4 5 6
# struct member offsets: shape, then the member offsets added after the first index
for case in "struct-vec3 16" "nested-vec3 16 16" "struct-half3 8" "struct-int3 16" \
            "struct-lead 16" "struct-vec4 16" "struct-vec2 8"; do
  set -- $case; shape=$1; shift
  "$OUT/physical-gep" $shape "$OUT/$shape.spv" >/dev/null || { echo "FAIL $shape translate"; continue; }
  got=$(spirv-dis "$OUT/$shape.spv" | grep -o 'OpIAdd %ulong %[a-z0-9_]* %ulong_[0-9]*' | grep -o '[0-9]*$' | paste -sd' ')
  [ "$got" = "$*" ] && echo "PASS $shape $got" || echo "FAIL $shape got '$got' want '$*'"
done
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/struct-vec3.spv" 4
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/nested-vec3.spv" 8
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/struct-lead.spv" 4 5 6
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/struct-vec4.spv" 4
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$OUT/struct-vec2.spv" 2
```

Before the change `vector` and `scalar` translate but `spirv-val` rejects them
(`OpAccessChain` result type does not match the type indexed from the base); `array` and `array32`
validate. After the change all three validate and the Apple M1 readback returns
the expected values (the input buffer holds 0..15).

`vec3` checks the first-index stride. LLVM's `getelementptr` steps the first index over whole
objects, i.e. by the source type's allocation size (its size rounded up to its alignment), and a
Metal `float3` is 12 bytes of data in a 16-byte, 16-aligned slot, so index 1 is byte 16. With the
unrounded size the multiply is by 12: `spirv-val` still accepts the module, the `grep` finds no
match, and the readback returns 3 4 5 instead of 4 5 6. `readback` also requires every output
element past the expected values to keep its -999 fill, so `vec3` also shows the store wrote
12 bytes and `scalar` 4.

The `struct-*` shapes check struct member offsets, which follow the same rule: a member starts
after the previous member's allocation size, as in LLVM's struct layout and Metal's (a `float3`
member occupies 16 bytes). With the unrounded size, `struct-vec3` and `struct-int3` put the second
member at 12, `struct-half3` at 6 and `nested-vec3` at 16 + 12 = 28, again without any `spirv-val`
error. `struct-lead`, `struct-vec4` and `struct-vec2` are controls whose offsets do not change.
