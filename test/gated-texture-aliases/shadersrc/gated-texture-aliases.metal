#include <metal_stdlib>
using namespace metal;

// Which of the source arguments the function has; they all take texture(0).
constant int kSourceType [[function_constant(0)]];
constant bool kSource2D = kSourceType == 0;
constant bool kSource2DMS = kSourceType == 1;
constant bool kSource3D = kSourceType == 2;
constant bool kSourceCube = kSourceType == 3;

kernel void read_source(texture2d<float> source2D [[texture(0), function_constant(kSource2D)]],
                        texture2d_ms<float> source2DMS [[texture(0), function_constant(kSource2DMS)]],
                        texture3d<float> source3D [[texture(0), function_constant(kSource3D)]],
                        texturecube<float> sourceCube [[texture(0), function_constant(kSourceCube)]],
                        sampler s [[sampler(0)]],
                        device float4* out [[buffer(0)]])
{
	float4 color = float4(0.0);
	switch (kSourceType) {
		case 0: color = source2D.read(uint2(0)); break;
		case 1: color = source2DMS.read(uint2(0), 0); break;
		case 2: color = source3D.read(uint3(0)); break;
		case 3: color = sourceCube.sample(s, float3(1.0, 0.0, 0.0)); break;
	}
	out[0] = color;
}
