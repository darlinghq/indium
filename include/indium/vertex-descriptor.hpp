#pragma once

#include <indium/base.hpp>
#include <indium/types.hpp>

#include <memory>
#include <vector>
#include <unordered_map>

namespace Indium {
	struct VertexBufferLayoutDescriptor {
		size_t stride = 0;
		VertexStepFunction stepFunction = VertexStepFunction::PerVertex;
		size_t stepRate = 1;
	};

	struct VertexAttributeDescriptor {
		VertexFormat format = VertexFormat::Invalid;
		size_t offset = 0;
		size_t bufferIndex = 0;
	};

	struct VertexDescriptor {
		std::unordered_map<size_t, VertexBufferLayoutDescriptor> layouts;
		std::unordered_map<size_t, VertexAttributeDescriptor> attributes;
	};
};
