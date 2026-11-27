#version 450
// The sample count the render pipeline sets, as get_num_samples() reads it; 1 when it sets none.
layout(constant_id = 262147) const uint numSamples = 1u;
// A function constant beside it, which the pipeline has to keep.
layout(constant_id = 3) const uint shade = 0u;
layout(location=0) out vec4 color;
void main() { color = vec4(float(numSamples * 40u) / 255.0, float(shade) / 255.0, 0, 1); }
