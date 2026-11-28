# comparison-sampler

`comparison-sampler.spv` and `comparison-sampler.reflect.json` are committed rather than produced by the build, as in
`basic-compute-mslc`. They were produced from `shadersrc/comparison-sampler.metal` with:

```sh
mslc -V --reflect comparison-sampler.reflect.json -o comparison-sampler.spv shadersrc/comparison-sampler.metal
```

against mslc at `d21275d`, which reported `spirv-val: PASS`. The test writes out the reflection by hand from the JSON.

`helper_compare` covers a comparison in a helper function that receives its sampler as a parameter, which mslc
emits as an `OpFunctionParameter`, and `gather_compare` (`OpImageDrefGather`).
