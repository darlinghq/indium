`nonphysical-gep.cpp` authors a fragment function through LLVM's C API that applies a
`getelementptr` (i8, offset 4) to its `texture2d` argument. Iridium translates the texture to an
image pointer in the UniformConstant storage class, which has no byte address. Nothing here comes
from third-party compiler output.

From the repository root (`$OUT` is a scratch directory):

```sh
c++ -std=c++17 -ffunction-sections -fdata-sections '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include test/nonphysical-gep/nonphysical-gep.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -Wl,--gc-sections -lLLVM -ldl -o "$OUT/nonphysical-gep"
"$OUT/nonphysical-gep" "$OUT/nonphysical-gep.spv" 2>"$OUT/nonphysical-gep.err"; echo "exit $?"
grep 'getelementptr on a non-physical pointer' "$OUT/nonphysical-gep.err"
```

Before the change the translator emitted pointer-to-integer arithmetic on the image pointer and
exited 0; `spirv-val` rejects that module (`OpConvertPtrToU` on a non-physical pointer). After, it
exits 1 and the `grep` finds the error. Every buffer argument is a physical pointer, so only
non-buffer pointers reach this case.
