The fixture authors complete LLVM-CAPI vertex functions with dynamic float/int scalar or four-component vector bitcasts and typed consumers. No compiled shader input is used.

```sh
c++ -std=c++17 -DHOST_LLVM_LIBNAME='"libLLVM.so"' -Iinclude -Iprivate-include test/bitcast-destination/bitcast-destination.cpp src/iridium/spirv.cpp src/iridium/dynamic-llvm.cpp -ldl -lLLVM-22 -o /tmp/bitcast-destination
for direction in 0 1; do
  for count in 1 4; do
    /tmp/bitcast-destination "$direction" "$count" "/tmp/authored-bitcast-$direction-$count.spv" || exit
    spirv-val --target-env vulkan1.2 "/tmp/authored-bitcast-$direction-$count.spv" || exit
  done
done
```

Adjust the LLVM link library to the installed version. Baseline all four modules fail operand/result type validation. Candidate all four validate. LLVM's published bitcast contract determines the result from the destination type, preserving the bit pattern and total width.

Opaque pointer pointee recovery is retained. Its preexisting equal-type logical pointer emission is independently reproduced and addressed separately; this numeric fix does not claim that path validates.
