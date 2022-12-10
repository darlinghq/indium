# General Development Notes

This is a place to write down some important notes to aid understanding the code. This is best used for concepts that affect many areas of the code (which would make a code comment in a single place ineffective).

## Metal terms to Vulkan terms

Here's the way I understand how some terms in each API map to each other:

| Metal | Vulkan |
| ----- | ------ |
| Thread | Invocation |
| SIMD-group | Subgroup |
| Threadgroup | Workgroup |
