#include <metal_stdlib>
using namespace metal;

struct VertexOut {
	float4 position [[position]];
};

vertex VertexOut fullscreen_vs(uint vid [[vertex_id]]) {
	VertexOut out;
	float2 uv = float2((vid << 1) & 2, vid & 2);
	out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
	return out;
}

fragment float4 color_fs(VertexOut in [[stage_in]]) {
	return float4(0.2, 0.4, 0.6, 0.8);
}

struct StencilOut {
	float4 color [[color(0)]];
	uint stencil [[stencil]];
};

fragment StencilOut stencil_fs(VertexOut in [[stage_in]]) {
	StencilOut out;
	out.color = float4(1.0, 0.0, 0.0, 1.0);
	out.stencil = 7;
	return out;
}
