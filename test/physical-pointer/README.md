# physical-pointer

Compute kernels that reach device and constant memory through pointers whose pointee the module doesn't
declare:

- `untyped_argument`: a buffer whose `air.arg_type_name` names a struct the module doesn't declare, read as
  an `int` and a `float`;
- `loaded_pointer`: a device pointer loaded from a buffer and read as `int`s and `float`s, and a field after
  it reached through the struct type that holds it (at byte 8);
- `constant_argument`: a device pointer loaded from a `constant` buffer;
- `stored_pointer`: a device pointer stored to device memory, loaded back and read through.

`physical-pointer.cpp` runs them and compares each thread's result with the value the kernel's comment
defines. `check.sh` (ctest `physical-pointer`) translates the library with `mtl2spv`, validates it with
`spirv-val`, checks in the disassembly that buffer arguments, `getelementptr` results and loaded and stored
addresses are all `uint` pointers in PhysicalStorageBuffer, and checks that each library in `refused/` fails
with the message its source names: an address loaded with 4-byte alignment, a constant variable's address
stored to device memory, and a texture and a sampler loaded from device memory.

## Provenance of the .metallib files

No Metal compiler was used. `shadersrc/physical-pointer.ll` and `refused/*.ll` are hand-written AIR, wrapped
in the MTLB container Iridium reads:

    llvm-as shadersrc/physical-pointer.ll -o physical-pointer.air
    ../air-ops/make-metallib.py physical-pointer.air physical-pointer.metallib kernel:untyped_argument kernel:loaded_pointer kernel:constant_argument kernel:stored_pointer
    llvm-as refused/stored-variable.ll -o stored-variable.air
    ../air-ops/make-metallib.py stored-variable.air refused/stored-variable.metallib kernel:stored_variable

and likewise for the other files in `refused/` (each has one kernel, named after the file with `_` for `-`). They were
assembled with LLVM 18, so that every LLVM Iridium supports can read them.
