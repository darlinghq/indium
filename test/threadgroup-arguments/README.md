# threadgroup-arguments

Kernel arguments in threadgroup memory (`threadgroup T *p [[threadgroup(n)]]`), whose length the host sets with
`setThreadgroupMemoryLength`. `reverse` reverses its threadgroup's elements through one, which only works when
the argument is as long as the length set; `pair` has two, with 16- and 4-byte elements and declared out of
index order, so each has to be sized by its own index and element size. The test also checks that a
reflection with unusable specialization IDs, indices or element sizes is refused.

`threadgroup-arguments.spv` is mslc's output for `threadgroup-arguments.metal`:

    mslc -o threadgroup-arguments.spv threadgroup-arguments.metal

mslc declares each argument as a Workgroup array whose length is a specialization constant (SpecIds 3, 4 and
5 here), and the reflection the test passes is what mslc's `--reflect` reports for the module.

Skips (prints SKIP, exits 0) when there is no Vulkan device.
