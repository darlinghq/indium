#include <metal_stdlib>
using namespace metal;

// Thread i < 3 reads level i of the 2D texture; thread 3 + f reads face f of the cube at level 1; thread 9
// writes sizes of the levels. Every level is read at a lod known only at run time.
kernel void fetchLevels(texture2d<float, access::read> flat [[texture(0)]],
                        texturecube<float, access::read> cube [[texture(1)]],
                        device float4* out [[buffer(0)]],
                        constant uint& cubeLevel [[buffer(1)]],
                        uint i [[thread_position_in_grid]]) {
	if (i < 3) {
		out[i] = flat.read(uint2(0), i);
	} else if (i < 9) {
		out[i] = cube.read(uint2(0), i - 3, cubeLevel);
	} else {
		out[i] = float4(flat.get_width(1), flat.get_height(2), cube.get_width(cubeLevel), cube.get_width());
	}
}
