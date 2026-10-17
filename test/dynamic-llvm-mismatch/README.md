Checks that `Iridium::DynamicLLVM::init()` returns false (with a message on stderr) instead of throwing when the loaded libLLVM reports a different major than the headers. Needs no Vulkan:

```sh
c++ -std=c++17 -fPIC -shared test/dynamic-llvm-mismatch/fake-llvm.cpp -o /tmp/fake-llvm.so
c++ -std=c++17 -DHOST_LLVM_LIBNAME='"/tmp/fake-llvm.so"' -Iinclude -Iprivate-include -I$(llvm-config --includedir) test/dynamic-llvm-mismatch/dynamic-llvm-mismatch.cpp src/iridium/dynamic-llvm.cpp -ldl -o /tmp/dynamic-llvm-mismatch
/tmp/dynamic-llvm-mismatch
```

Before the change the program reports `init() threw`; after, it prints the mismatch message and `PASS`.
