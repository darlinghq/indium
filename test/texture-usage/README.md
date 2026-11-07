This authored native guest observes the public `vkCreateImage` input from
Indium's existing dynamic Vulkan dispatch. It delegates the call unchanged.
No shader or binary instructions are read.

With standalone Indium ENABLE_TESTS, build the indium-test-texture-usage target.
Compile/link the native guest against the configured Indium target using the same
production include flags and libindium. In the private reproduction workspace:

    python3 /home/cristi/tmp-opencode/metal-resume/build-cpp-probe.py test/texture-usage/texture-usage.cpp /home/cristi/tmp-opencode/metal-resume/texture-usage-expanded
    cp /home/cristi/tmp-opencode/metal-resume/texture-usage-expanded /home/cristi/tmp-opencode/metal-resume/probe-prefix/Applications/texture-usage-expanded
    DPREFIX=/home/cristi/tmp-opencode/metal-resume/probe-prefix DARLING_INSTALL_PREFIX=/home/cristi/tmp-opencode/metal-resume/image/usr/local /home/cristi/tmp-opencode/metal-resume/build/src/startup/darling shell /Applications/texture-usage-expanded

Baseline on Apple M1 reports seven failures and exits 1: declared read/write/
render flags are ignored, unknown depth requests an unsupported storage usage,
and invalid/unsupported usages reach allocation. Candidate must pass all checks.
The program prints the actual selected device.

PixelFormatView and multisample ShaderWrite requests remain explicitly
unsupported. Unknown uses the format's supported feature union, then validates
the complete combination; unsupported combinations fail rather than silently
remove required operations. Transfer usage remains required for existing
getBytes/replaceRegion operations. Native draw fixtures must declare RenderTarget
usage instead of relying on the default ShaderRead descriptor.
