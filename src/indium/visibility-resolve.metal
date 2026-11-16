// Copyright (C) 2026 Lubos Dolezel
#include <metal_stdlib>
using namespace metal;

// One entry per visibility result span of a render encoder, in the order the spans were set.
struct Span {
	uint slot;   // index of the 8-byte result slot in the visibility result buffer
	uint mode;   // 1: boolean, 2: counting
	uint query;  // index of the span's occlusion query result in `queries`
	uint first;  // 1 when this is the first span of the encoder that uses the slot
};

// Folds the occlusion query results of a render pass into the visibility result buffer. A slot is
// cleared by the first span that uses it; a counting span adds its sample count, a boolean span sets the
// slot to 1 if any sample passed. Counts are 64-bit, kept as low and high words.
kernel void visibilityResolve(
	device uint2* results [[buffer(0)]],
	constant Span* spans [[buffer(1)]],
	device const uint2* queries [[buffer(2)]],
	constant uint& spanCount [[buffer(3)]])
{
	for (uint i = 0; i < spanCount; i++) {
		Span s = spans[i];
		uint2 value = s.first != 0 ? uint2(0, 0) : results[s.slot];
		uint2 q = queries[s.query];
		if (s.mode == 2) {
			uint low = value.x + q.x;
			value = uint2(low, value.y + q.y + (low < value.x ? 1 : 0));
		} else if ((q.x | q.y) != 0) {
			value = uint2(1, 0);
		}
		results[s.slot] = value;
	}
}
