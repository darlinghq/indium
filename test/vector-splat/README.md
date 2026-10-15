Floating-point vector splats (`zeroinitializer`, `splat (float 1.5)`) arrive as vector-typed ConstantFP values on LLVM 23, and as ConstantAggregateZero or ConstantDataVector on older LLVM. This fixture writes six such constants as LLVM IR, translates each with `llvmValueToResultID`, and checks that the result is one OpConstantComposite whose lanes are the same OpConstant with the expected bit pattern (float, double and half).

```sh
c++ -std=c++17 -DHOST_LLVM_LIBNAME='"libLLVM.so"' -Iinclude -Iprivate-include test/vector-splat/vector-splat.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -ldl -lLLVM -o /tmp/vector-splat
/tmp/vector-splat
```

Adjust the LLVM link library to the installed version. On LLVM 23 the baseline fails all six cases with "unsupported float constant type kind 13"; the candidate passes all six. Older LLVM passes either way, because those constants take the existing aggregate paths.
