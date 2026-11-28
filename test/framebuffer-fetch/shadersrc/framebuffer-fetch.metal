// Framebuffer fetch: fragment functions that read a color attachment of the render pass ([[color(n)]]).
#include <metal_stdlib>
using namespace metal;

// a triangle that covers the render target
vertex float4 fullscreen_vs(uint vertexID [[vertex_id]]) {
  float2 corner = float2((vertexID << 1) & 2, vertexID & 2);
  return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

// reads the attachment it writes and inverts the color
fragment float4 invert_fs(float4 c [[color(0)]]) {
  return float4(1.0 - c.rgb, c.a);
}

// reads color attachment 1 and writes it to color attachment 0
struct CopyOut {
  float4 target [[color(0)]];
};
fragment CopyOut copy_from_1_fs(float4 source [[color(1)]]) {
  CopyOut out;
  out.target = source;
  return out;
}

// no fetch: a constant color
fragment float4 constant_fs() {
  return float4(0.25, 0.5, 0.75, 1.0);
}

// an integer attachment: reads the unsigned texel and adds one to each channel
fragment uint4 add_one_uint_fs(uint4 c [[color(0)]]) {
  return c + uint4(1);
}

// reads and writes color attachment 1 of a pass whose attachment 0 is empty
struct Slot1Out {
  float4 target [[color(1)]];
};
fragment Slot1Out invert_1_fs(float4 c [[color(1)]]) {
  Slot1Out out;
  out.target = float4(1.0 - c.rgb, c.a);
  return out;
}
