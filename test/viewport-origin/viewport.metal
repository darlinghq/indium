// SPDX-License-Identifier: 0BSD
#include <metal_stdlib>
using namespace metal;

struct VertexOutput {
	float4 position [[position]];
};

vertex VertexOutput rectangleVertex(uint index [[vertex_id]]) {
	VertexOutput output;
	bool right = index == 1 || index == 4 || index == 5;
	bool top = index == 2 || index == 3 || index == 5;
	output.position = float4(right ? 1.0 : -1.0, top ? 1.0 : -1.0, 0.5, 1.0);
	return output;
}

fragment float4 rectangleFragment() {
	return float4(1.0, 0.0, 0.0, 1.0);
}
