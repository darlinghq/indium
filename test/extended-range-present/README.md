# extended-range-present

A layer in `BGRA10_XR`, `BGRA10_XR_sRGB`, `BGR10_XR` or `BGR10_XR_sRGB` presented to an Xlib window. The window is read back
from the X server, whose surface has 8 bits a channel, so the test checks what a display that cannot show beyond 0 to 1
shows: the gamma encoded values of `BGRA10_XR` and `BGR10_XR` as they are, the linear values of the sRGB formats through the
sRGB curve, values outside the range clamped, and the 8-bit `BGRA8Unorm` layer unchanged. Needs `DISPLAY`; prints SKIP
without a display or a device that can present to it.

A surface that offers `R16G16B16A16_SFLOAT` in the extended linear sRGB color space gets the values as linear light instead
(see `IndiumKit::PrivateLayer::chooseExtendedRangeTarget`); an X server does not offer it, so that path is not exercised
by this test.
