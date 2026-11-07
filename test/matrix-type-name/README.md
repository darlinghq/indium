These tests call the existing metadata type mapper with authored type names and
LLVM types created through LLVM's C API. They never load a guest metallib or AIR,
and never read or print shader instructions.

From the Indium root on Linux:

    c++ -std=c++17 -ffunction-sections -fdata-sections -Iinclude -Iprivate-include test/matrix-type-name/first-dimension.cpp -Wl,--gc-sections -o /tmp/first-dimension
    /tmp/first-dimension

Before the fix this prints `FAIL ... first dimension=1124` for `float4x4`
and exits 1. After the fix it prints dimension 4 and exits 0.

The wider shape/layout and invalid-input checks use the installed LLVM library:

    c++ -std=c++17 -ffunction-sections -fdata-sections '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include test/matrix-type-name/matrix-type-name.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -Wl,--gc-sections -lLLVM -ldl -o /tmp/matrix-type-name
    /tmp/matrix-type-name

Including the implementation keeps these internal helpers private; section
collection discards unrelated translator functions in these standalone tests.
