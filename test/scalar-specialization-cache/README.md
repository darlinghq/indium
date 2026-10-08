The authored builder fixture checks that ordinary constants never alias specialization constants, in either declaration order. It also checks ordinary deduplication and independent specialization IDs. No compiled shader input is used.

From the repository root:

```sh
c++ -std=c++17 -Iinclude -Iprivate-include test/scalar-specialization-cache/scalar-specialization-cache.cpp src/iridium/spirv.cpp -o /tmp/scalar-specialization-cache
/tmp/scalar-specialization-cache
```

Before the fix, specialized-first fails and exits 1. After the fix both orders pass and exit 0.
