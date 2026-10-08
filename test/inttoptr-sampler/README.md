`inttoptr-sampler.cpp` authors two fragment functions through LLVM's C API. Each
samples a `texture2d<float>` with a constant sampler that is referenced directly as
an integer-to-pointer constant expression (`inttoptr (i64 <packed state> to ptr
addrspace(2))`) instead of through a global variable named by `air.sampler_states`.
The packed state uses the layout already documented in `air.cpp`
(repeat / clamp to edge / clamp to edge, linear magnification, nearest
minification, no mip filter, normalized, compare never, LOD 0 to 10). Constants are
uniqued per context, so both functions share the one expression; each function must still
get its own sampler variable and list it as an interface variable. Nothing in it comes from
third-party compiler output.

From the repository root (`$OUT` is a scratch directory):

```sh
c++ -std=c++17 -ffunction-sections -fdata-sections '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include test/inttoptr-sampler/inttoptr-sampler.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -Wl,--gc-sections -lLLVM -ldl -o "$OUT/inttoptr-sampler"
"$OUT/inttoptr-sampler" "$OUT/inttoptr-sampler.spv" && spirv-val --target-env vulkan1.3 --relax-block-layout --scalar-block-layout "$OUT/inttoptr-sampler.spv"
```

Before the change: `unsupported LLVM value kind 10`, exit 1 (the translation of a
shader library that uses such a sampler fails and the application gets no default
library). After: each function reports one embedded sampler with exactly the authored state, the
module validates, exit 0. A version that associated the sampler with the shared expression
instead of keeping it per function reported the same state but failed validation (the second
function used the first one's variable without listing it).
