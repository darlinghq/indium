#pragma once

#include <indium/base.hpp>
#include <indium/types.hpp>

#include <memory>
#include <vector>

namespace Indium {
	struct VertexBufferLayoutDescriptor {
		size_t stride = 0;
		VertexStepFunction stepFunction = VertexStepFunction::PerVertex;
		size_t stepRate = 1;
	};

	struct VertexAttributeDescriptor {
		VertexFormat format;
		size_t offset;
		size_t bufferIndex;
	};

	struct VertexDescriptor {
		std::vector<VertexBufferLayoutDescriptor> layouts;
		std::vector<VertexAttributeDescriptor> attributes;
	};
};
