`air-convert.cpp` authors a vertex function through LLVM's C API that calls
`air.convert.f.<to>.f.<from>` on a constant, for scalar and four-component floating
types of different widths. Nothing here comes from third-party compiler output.

From the repository root (`$OUT` is a scratch directory):

```sh
c++ -std=c++17 -DHOST_LLVM_LIBNAME='"libLLVM.so"' -Iinclude -Iprivate-include test/air-convert/air-convert.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -lLLVM -ldl -o "$OUT/air-convert"
for pair in "f16 f32" "f32 f16" "v4f16 v4f32" "v4f32 v4f16"; do
  set -- $pair
  "$OUT/air-convert" $1 $2 "$OUT/convert.spv" && spirv-val --target-env vulkan1.3 "$OUT/convert.spv" || echo "FAIL $pair"
done
"$OUT/air-convert" f32 f32 "$OUT/same.spv"   # same width: still rejected, exit 1
for pair in "f32 f16." "f32 f16.extra" "vf32 vf16" "v0f32 v0f16" "v1f32 v1f16" "v5f32 v5f16" "v8f32 v8f16" "v16f32 v16f16" "v32f32 v32f16" "f32 f64" "f64 f32" "v4f64 v4f32"; do
  set -- $pair
  "$OUT/air-convert" $1 $2 "$OUT/malformed.spv" 2>"$OUT/malformed.err"
  grep -q 'TODO: support actual function calls' "$OUT/malformed.err" || echo "FAIL not rejected by name: $pair"
done
```

The last loop names conversions with a trailing component (`air.convert.f.f32.f.f16.extra`), a
trailing dot, or a vector prefix whose lane count is missing or not 2, 3 or 4 (the Metal vector
sizes, per the MSL specification's vector data types), and the f64 names: the MSL
specification's scalar types have no `double`, so no Metal shader produces them. Each must fail the name check, so the
translator falls through to its unknown-call error, rather than being lowered as a plain conversion
that drops or misreads part of the name. The `grep` makes sure the failure is that error and not
some other exception. The harness builds a placeholder LLVM type for such names; only the name
matters here.

Before the change the scalar pairs fail with `TODO: support actual function calls`
(only the two four-component pairs were recognised by name). After, every pair
translates and validates; the same-width case still exits 1.
