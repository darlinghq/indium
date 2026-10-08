`air-convert.cpp` authors a vertex function through LLVM's C API that calls
`air.convert.f.<to>.f.<from>` on a constant, for scalar and four-component floating
types of different widths. Nothing here comes from third-party compiler output.

From the repository root (`$OUT` is a scratch directory):

```sh
c++ -std=c++17 -DHOST_LLVM_LIBNAME='"libLLVM.so"' -Iinclude -Iprivate-include test/air-convert/air-convert.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -lLLVM -ldl -o "$OUT/air-convert"
for pair in "f16 f32" "f32 f16" "f32 f64" "v4f16 v4f32" "v4f32 v4f16"; do
  set -- $pair
  "$OUT/air-convert" $1 $2 "$OUT/convert.spv" && spirv-val --target-env vulkan1.3 "$OUT/convert.spv" || echo "FAIL $pair"
done
"$OUT/air-convert" f32 f32 "$OUT/same.spv"   # same width: still rejected, exit 1
```

Before the change the scalar pairs fail with `TODO: support actual function calls`
(only the two four-component pairs were recognised by name). After, every pair
translates and validates; the same-width case still exits 1.
