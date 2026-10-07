# Multisample color resolve

This shaderless public Indium API test clears a color attachment red and blue,
then checks every RGBA8 byte in a shared single-sample result texture. The 1x
control stores directly; the 4x case resolves a private multisample attachment.
A fullscreen blue GLSL draw also checks the pipeline sample count and resolve
path. glslangValidator builds these authored shaders during test compilation;
no shader binaries are committed. An untouched result cannot pass: it starts with a poison pattern.

Build the `indium-test-multisample-resolve` target with Indium tests enabled and
run it with a Vulkan device. Under Darling, use the build-tree non-setuid launcher
and a private staged runtime/prefix. No application binaries are required.

Before the fix, 1x clears pass and all three 4x operations fail all 768 bytes. A native
CAMetalLayer fixture independently presents red for 1x but black for 4x.

Specification: the existing RenderPassAttachmentDescriptor and TextureDescriptor
headers (rung 2), Vulkan VkSubpassDescription, VkImageCreateInfo,
vkGetPhysicalDeviceImageFormatProperties and VkPipelineMultisampleStateCreateInfo
public documentation (rung 3), and this byte-exact API observation (rung 4).
