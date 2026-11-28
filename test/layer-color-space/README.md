# layer-color-space

`IndiumKit::Layer::setColorSpace()`: a layer whose values are linear light (`ExtendedLinearSRGB`) or sRGB encoded values
extended beyond 0 to 1 (`ExtendedSRGB`) shows the right colors on a surface that cannot present those color spaces itself.
The window of the test is read back from the X server, whose surface is 8-bit sRGB (it offers neither `R16G16B16A16_SFLOAT` nor
a color space beyond `SRGB_NONLINEAR`), so the conversion the layer falls back to is what is checked: linear values go through the
sRGB curve, encoded ones stay as they are, values outside 0 to 1 are clamped, with extended dynamic range on or off. The color
spaces without a conversion (Display P3 and BT.2020, linear) are refused, and the layer keeps what it had. The same clear in
`BGRA8Unorm` with the default color space shows what it did before the color space existed. Needs `DISPLAY`; prints SKIP without a
display or a device that can present to it. Surfaces that offer the color spaces (a Wayland compositor with color management) present
the values themselves; that path is covered by the guest test of the CAMetalLayer on top of this.
