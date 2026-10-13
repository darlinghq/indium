`physical-gep.cpp` authors a compute kernel with LLVM's C API: it loads from a
`float4x4` buffer through a `getelementptr` whose source element type is not the
buffer's declared pointee, and stores the result. Shapes: `vector` (`<4 x float>`
source, index 1), `scalar` (`float` source, index 5) and `array` (`[4 x <4 x float>]`
source, i64 indices 0 and 1, the declared layout, as a control), `array32` (the same with i32 indices)
and `vec3` (`<3 x float>` source, index 1). Nothing here comes from third-party compiler output.

Each shape lists its source type, indices, loaded type and argument names in
`physical-gep.cpp`. The fixture sets the AIR datalayout on the module and prints the byte
constants the GEP must use, computed by LLVM-C (`LLVMABISizeOfType`, `LLVMOffsetOfElement`
under that datalayout); `check.sh` compares them with the `OpIMul`/`OpIAdd` constants in the
disassembly, runs `spirv-val` on the 32-bit rows and fails on any mismatch. Load alignment is
the loaded type's ABI alignment capped by the largest power of two dividing the byte offset.
With `-DENABLE_TESTS=ON` (which then requires `spirv-dis` and `spirv-val` from SPIRV-Tools
at configure time), build `indium-test-physical-gep` and run `ctest -R physical-gep` from the
build directory. By hand, from the
repository root (`$OUT` is a scratch directory):

```sh
c++ -std=c++17 -ffunction-sections -fdata-sections '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include test/physical-gep/physical-gep.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -Wl,--gc-sections -lLLVM -ldl -o "$OUT/physical-gep"
test/physical-gep/check.sh "$OUT/physical-gep" "$OUT"
```

Readback needs the Apple M1 Vulkan device and stays manual (the input buffer holds 0..15):

```sh
c++ -std=c++17 test/physical-gep/readback.cpp -lvulkan -o "$OUT/readback"
rb() { VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json "$OUT/readback" "$@"; }
rb "$OUT/neg32.spv" 4
rb "$OUT/vector.spv" 4 5 6 7
rb "$OUT/scalar.spv" 5
rb "$OUT/array.spv" 4 5 6 7
rb "$OUT/array32.spv" 4 5 6 7
rb "$OUT/vec3.spv" 4 5 6
rb "$OUT/struct-vec3.spv" 4
rb "$OUT/nested-vec3.spv" 8
rb "$OUT/struct-lead.spv" 4 5 6
rb "$OUT/struct-vec4.spv" 4
rb "$OUT/struct-vec2.spv" 2
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

`neg32` steps back one float from element 5 with an i32 index of -1. The index must be
sign-extended to 64 bits before the byte multiply (`OpSConvert`); zero-extension would add
almost 2^34 bytes instead of subtracting 4. The readback returns element 4.
