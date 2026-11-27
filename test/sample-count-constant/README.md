# Sample count specialization constant

A fragment function whose reflection names `numSamplesSpecializationID` reads
the render pipeline's raster sample count in that specialization constant, the
value MSL's `get_num_samples()` returns. The GLSL fragment shader writes 40
times the constant to red and a function constant to green, so the pipeline has
to merge the sample count into the function's own specialization. The test
draws at 1x and 4x (resolved), each with and without the reflection; without it
the constant keeps its default of 1. It also checks that a vertex function
with the constant, and a constant whose id a function constant uses, are
refused. glslangValidator builds the shaders during test compilation.

Build the `indium-test-sample-count-constant` target with Indium tests enabled
and run it with a Vulkan device.

Specification: the Metal Shading Language specification's get_num_samples()
(rung 2), Vulkan VkSpecializationInfo and VkPipelineMultisampleStateCreateInfo
public documentation (rung 3), and this byte-exact API observation (rung 4).
