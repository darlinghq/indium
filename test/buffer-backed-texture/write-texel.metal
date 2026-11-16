#include <metal_stdlib>
using namespace metal;
kernel void writeTexel(texture2d<float, access::write> tex [[texture(0)]], uint2 gid [[thread_position_in_grid]]) {
	tex.write(float4(float(gid.x) / 10.0, float(gid.y) / 10.0, 0.5, 1.0), gid);
}
