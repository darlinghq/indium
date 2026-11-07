# texture-readback

Byte-exact verification of `Indium::Texture::getBytes`, the read direction of
`replaceRegion`, which exists so the Metal layer can implement
`-[MTLTexture getBytes:bytesPerRow:fromRegion:mipmapLevel:]` and
`getBytes:bytesPerRow:bytesPerImage:fromRegion:mipmapLevel:slice:`.

A texture built from the default `TextureDescriptor` has
`allowGPUOptimizedContents = true`, so its image is `VK_IMAGE_TILING_OPTIMAL` and
its memory cannot be mapped. The readback therefore cannot be a host pointer into
the image: it has to be a GPU copy into a host-visible buffer, submitted and
waited on, which is the shape `replaceRegion` already has going the other way.
This fixture is what exercises that path end to end on a real device.

## What is checked, and against what

Every check is byte-for-byte against the pattern that was uploaded by
`replaceRegion`, not against a re-derivation of it, so a readback that came back
correct but from the wrong place is still wrong.

- the whole level of an 8x6 `RGBA8Unorm` texture, 192 of 192 bytes;
- a 3x4 sub-region at (2,1), against that sub-rectangle of the pattern, which is
  what shows the origin and the extent are both honoured;
- a `bytesPerRow` two whole texels wider than the row, which must leave the
  padding untouched and is the case `bufferRowLength` exists for;
- the storage-mode precondition: `StorageModePrivate` must throw rather than
  abort the process, and must leave the destination untouched;
- the `bytesPerRow` precondition, below.

The pattern's bytes all differ from their neighbours in every channel and the
alpha channel is written too, so a transposed, shifted, row-swapped or
wrong-texel-size readback cannot land on the right answer.

## Negative controls

Each positive check has a control that has to fail.

- The identical comparison against a one-byte-corrupted copy of the pattern must
  be rejected, or "byte exact" is not a claim this test could make.
- Two readbacks of the same texture must be identical, so both sides of the
  comparison cannot have come from one call.
- A readback into a buffer pre-filled with `0xAA` must have overwritten every
  byte, so "matched" is distinguishable from "left alone".
- The sub-region read must differ from the same-sized read at the origin, and
  from the same-origin read with a larger extent, so neither field is being
  ignored.
- A padded readback must differ from a packed one, and neither may throw.
- The same call on a `StorageModeShared` texture must not throw, so the private
  check is about the storage mode and not about `getBytes` always refusing.

## The two preconditions, and why they are errors

`StorageMode` must be `Shared` or `Managed`. A private texture has no
host-visible contents, and Metal does not define `getBytes:` for one, so the call
throws `std::runtime_error` instead of inventing bytes or aborting. The same
precondition and the same error shape as `replaceRegion`.

`bytesPerRow` must be a whole number of texels. `VkBufferImageCopy`'s
`bufferRowLength` is in texels, so a byte pitch that is not a multiple of the
texel size has no representation in the copy indium encodes. Rounding it down
would hand back a buffer laid out at a stride the caller never asked for, in
silence. That is not hypothetical: the padded-pitch check originally asked for 39
bytes per row on a 4-byte texel, and 154 of the 192 bytes came back in the wrong
place before the check existed.

`replaceRegion` and the blit encoder convert the same byte pitch the same way and
still round down. That is pre-existing and untouched here; only the new API
refuses it.

## Running it

```
cmake -G Ninja -DENABLE_TESTS=ON <build>
ninja indium-test-texture-readback
./test/texture-readback/indium-test-texture-readback
```

No metallib, no GL window and no shader: the test only needs a device and some
textures. The process exits with `_exit` once the verdict is printed, for the
teardown reason given in `test/sampler-shim/sampler-semantics.cpp`.
