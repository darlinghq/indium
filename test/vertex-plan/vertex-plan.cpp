// Copyright (C) 2026 Lubos Dolezel
//
// The vertex input state a render pipeline gives Vulkan (VertexInputPlan): constant step function
// layouts become stride-zero bindings, only the attributes the vertex function reads and the layouts
// they use are kept, and a binding is only allowed to fall back to the zero-filled buffer when every
// fetch reads the same bytes inside it. Host test, no GPU. Build and run from the repository root:
//
//   c++ -std=c++17 -Iinclude -Iprivate-include test/vertex-plan/vertex-plan.cpp -o vertex-plan && ./vertex-plan

#include <indium/vertex-plan.private.hpp>

#include <cstdio>

namespace {

using namespace Indium;

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

// A per-vertex position stream at buffer 0 and the described-but-unused attributes a descriptor
// commonly carries.
VertexDescriptor descriptor() {
	VertexDescriptor d;
	d.layouts[0] = { 16, VertexStepFunction::PerVertex, 1 };
	d.layouts[1] = { 8, VertexStepFunction::PerInstance, 1 };
	d.layouts[2] = { 0, VertexStepFunction::Constant, 0 };
	d.attributes[0] = { VertexFormat::Float4, 0, 0 };
	d.attributes[1] = { VertexFormat::Float2, 0, 1 };
	d.attributes[2] = { VertexFormat::Float4, 16, 2 };
	return d;
}

bool throws(const VertexDescriptor& d, const std::optional<std::unordered_set<size_t>>& consumed) {
	try {
		planVertexInput(d, consumed, 65536);
	} catch (const std::runtime_error&) {
		return true;
	}
	return false;
}

}

int main() {
	// every attribute and layout when nothing says what the shader reads
	{
		auto plan = planVertexInput(descriptor(), std::nullopt, 65536);
		report(plan.bindings.size() == 3 && plan.attributes.size() == 3, "without reflection every attribute and layout is kept");
		report(plan.bindings[0].metalIndex == 0 && plan.bindings[1].metalIndex == 1 && plan.bindings[2].metalIndex == 2, "bindings are in buffer index order");
		report(plan.attributes[0].location == 0 && plan.attributes[1].location == 1 && plan.attributes[2].location == 2, "attributes are in location order");
	}

	// only what the shader reads
	{
		auto plan = planVertexInput(descriptor(), std::unordered_set<size_t> { 0, 2 }, 65536);
		report(plan.attributes.size() == 2 && plan.attributes[0].location == 0 && plan.attributes[1].location == 2, "unread attributes are dropped");
		report(plan.bindings.size() == 2 && plan.bindings[0].metalIndex == 0 && plan.bindings[1].metalIndex == 2, "layouts no read attribute uses are dropped");
		report(plan.attributes[1].binding == 1 && plan.attributes[1].offset == 16, "an attribute points at its compacted binding");
	}
	{
		auto plan = planVertexInput(descriptor(), std::unordered_set<size_t> {}, 65536);
		report(plan.bindings.empty() && plan.attributes.empty(), "a shader that reads nothing needs no vertex input");
	}
	{
		auto plan = planVertexInput(descriptor(), std::unordered_set<size_t> { 7 }, 65536);
		report(plan.bindings.empty() && plan.attributes.empty(), "a location the descriptor does not describe adds nothing");
	}

	// step functions
	{
		auto plan = planVertexInput(descriptor(), std::nullopt, 65536);
		report(plan.bindings[0].stride == 16 && !plan.bindings[0].perInstance, "per-vertex keeps its stride");
		report(plan.bindings[1].stride == 8 && plan.bindings[1].perInstance, "per-instance keeps its stride and rate");
		report(plan.bindings[2].stride == 0 && !plan.bindings[2].perInstance, "constant is stride zero");
	}
	{
		auto d = descriptor();
		d.layouts[2] = { 48, VertexStepFunction::Constant, 1 };
		auto plan = planVertexInput(d, std::nullopt, 65536);
		report(plan.bindings[2].stride == 0, "constant is stride zero whatever its stride and step rate");
	}
	{
		auto d = descriptor();
		d.layouts[0].stepRate = 2;
		report(throws(d, std::nullopt), "a per-vertex step rate other than 1 is rejected");
		report(!throws(d, std::unordered_set<size_t> { 2 }), "but not when no read attribute uses that layout");
		d = descriptor();
		d.layouts[1].stepFunction = VertexStepFunction::PerPatch;
		report(throws(d, std::nullopt), "per-patch is rejected");
	}

	// an attribute without a layout
	{
		auto d = descriptor();
		d.attributes[3] = { VertexFormat::Float, 0, 5 };
		report(throws(d, std::nullopt), "an attribute using a buffer without a layout is rejected");
		report(!throws(d, std::unordered_set<size_t> { 0 }), "unless the shader does not read it");
	}

	// attributes without a format are not described
	{
		auto d = descriptor();
		d.attributes[1].format = VertexFormat::Invalid;
		auto plan = planVertexInput(d, std::nullopt, 65536);
		report(plan.attributes.size() == 2 && plan.bindings.size() == 2, "an attribute without a format is ignored");
	}

	// the zero-buffer fallback
	{
		auto plan = planVertexInput(descriptor(), std::nullopt, 65536);
		report(!plan.bindings[0].fallbackSafe, "a per-vertex binding cannot use the zero buffer");
		report(!plan.bindings[1].fallbackSafe, "a per-instance binding cannot use the zero buffer");
		report(plan.bindings[2].fallbackSafe, "a constant binding can");
	}
	{
		auto d = descriptor();
		d.layouts[0] = { 0, VertexStepFunction::PerVertex, 1 };
		auto plan = planVertexInput(d, std::nullopt, 65536);
		report(plan.bindings[0].fallbackSafe, "a stride-zero per-vertex binding reads the same bytes too");
	}
	{
		auto d = descriptor();
		d.attributes[2].offset = 65536 - 16;
		report(planVertexInput(d, std::nullopt, 65536).bindings[2].fallbackSafe, "an attribute ending exactly at the end of the zero buffer fits");
		d.attributes[2].offset = 65536 - 15;
		report(!planVertexInput(d, std::nullopt, 65536).bindings[2].fallbackSafe, "an attribute one byte past the end does not");
		d.attributes[2].offset = SIZE_MAX - 3;
		report(!planVertexInput(d, std::nullopt, 65536).bindings[2].fallbackSafe, "an offset near SIZE_MAX does not overflow into fitting");
	}

	// attribute sizes
	{
		bool sizes = vertexFormatSize(VertexFormat::UChar) == 1 && vertexFormatSize(VertexFormat::UChar3) == 3
			&& vertexFormatSize(VertexFormat::Half3) == 6 && vertexFormatSize(VertexFormat::Float) == 4
			&& vertexFormatSize(VertexFormat::UChar4Normalized_BGRA) == 4 && vertexFormatSize(VertexFormat::Int1010102Normalized) == 4
			&& vertexFormatSize(VertexFormat::Float3) == 12 && vertexFormatSize(VertexFormat::UInt4) == 16
			&& vertexFormatSize(VertexFormat::Invalid) == 0;
		report(sizes, "vertex format sizes");
	}

	return failures ? 1 : 0;
}
