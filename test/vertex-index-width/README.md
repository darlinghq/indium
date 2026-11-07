The fixture creates its entire LLVM module through LLVM's C API: one authored
vertex-index parameter and, for narrow inputs, zero extension to 32 bits. It
constructs analyzer metadata from the existing OSS parser interface, never from
third-party compiler output. Only this authored translated output is validated.

From the Indium root:

    c++ -std=c++17 -ffunction-sections -fdata-sections '-DHOST_LLVM_LIBNAME="libLLVM.so"' -Iinclude -Iprivate-include test/vertex-index-width/vertex-index-width.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -Wl,--gc-sections -lLLVM -ldl -o /tmp/vertex-index-width
    /tmp/vertex-index-width 16 /tmp/authored-vertex16.spv
    spirv-val /tmp/authored-vertex16.spv
    /tmp/vertex-index-width 32 /tmp/authored-vertex32.spv
    spirv-val /tmp/authored-vertex32.spv
    /tmp/vertex-index-width 8 /tmp/authored-invalid8.spv
    /tmp/vertex-index-width 64 /tmp/authored-invalid64.spv

Before the fix, the 16-bit case translates but validation exits 1: its zero
extension gets a 32-bit operand instead of 16 bits. The 32-bit control validates.
After the fix both supported widths translate and validate with exit 0.
Unsupported 8/64-bit widths return an explicit error and exit 1.
