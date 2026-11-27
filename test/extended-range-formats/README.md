# extended-range-formats

The extended-range pixel formats `BGRA10_XR`, `BGRA10_XR_sRGB`, `BGR10_XR` and `BGR10_XR_sRGB` of a texture: what
it stores and returns. A texture of these formats is an `R32G32B32A32_SFLOAT` image of the values a shader sees (see
`private-include/indium/extended-range.private.hpp`), and the 10-bit codes of the documented layouts only appear in
`replaceRegion` and `getBytes`.

The test checks, for each format:

- every code of every channel survives `replaceRegion` followed by `getBytes`, in the documented 8- and 4-byte layouts, and
  padding bits that are not zero are not stored;
- a shader reads `(code - 384) / 510` (through the sRGB curve in the sRGB formats), alpha clamped to 0 to 1, and no
  alpha reads as 1;
- linear filtering of the sRGB formats happens on the values after the curve;
- what a render pass writes (shader output beyond the range, a clear color, blending) is stored as the nearest code
  within the range of the format;
- no view of another format, and no copy between a buffer and the texture;
- the compute pass that converts gamma encoded values to linear light for presentation.

## Where the numbers come from

`macos-27.0.1-arm64.txt` is the output of `mac-probe.m` on macOS 27.0.1 (26A434), arm64, a real Metal device. `gen-fixtures.py`
turns it into `fixtures.hpp` and `fill-values.glsl`, so the expected values are observations (black-box, through the public API):
the values a shader reads for each code, the codes stored for rendered values (inside and beyond the range, a NaN, near-ties),
a clear color and a blend. What the observations showed beyond the documentation:

- a value is stored up to code 1023 (1.25294), not 1022; alpha codes stay within 384 to 894;
- a NaN is stored as the code of 0; a value a hair above the middle of two codes is stored as the lower one;
- the sRGB formats' sampled values are multiples of 2^-13, so those are compared to one such step, and an sRGB conversion
  or a blend may differ by one code between devices;
- the source color is clamped to the range of the format before it is blended (not done here: blue of the blend case is not checked);
- `getBytes` returns the bytes `replaceRegion` was given, including padding bits and alpha codes outside 384 to 894. This
  implementation holds shader values, so it does not (known differences, pinned by the tests as such).

## Running

Needs a Vulkan device (lavapipe is enough); `VK_ICD_FILENAMES=<lvp_icd.json> indium-test-extended-range-formats`.
