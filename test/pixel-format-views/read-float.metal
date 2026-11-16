#include <metal_stdlib>
using namespace metal;
kernel void readFloat(texture2d<float> tex [[texture(0)]], device float4* out [[buffer(0)]]) {
	out[0] = tex.read(uint2(0, 0));
}
