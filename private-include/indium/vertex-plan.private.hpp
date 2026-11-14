// Copyright (C) 2026 Lubos Dolezel
//
// Which vertex buffer bindings and attributes a render pipeline gives Vulkan. Plain C++ over the
// vertex descriptor types (no Vulkan), so test/vertex-plan checks it on the host.

#pragma once

#include <indium/vertex-descriptor.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace Indium {
	/**
	 * The vertex input state of one pipeline: a binding for each Metal vertex buffer layout that a
	 * consumed attribute reads from, and the consumed attributes.
	 */
	struct VertexInputPlan {
		struct Binding {
			/** The Metal buffer index the application binds the data to. */
			size_t metalIndex = 0;

			/** Zero for a constant step function: every vertex and instance reads the same element. */
			size_t stride = 0;

			bool perInstance = false;

			/**
			 * Whether the device's zero-filled fallback buffer can stand in when the application
			 * binds nothing at `metalIndex`: only when every fetch reads the same bytes (stride 0)
			 * and the attributes lie within the buffer. A binding that advances through the buffer
			 * would read past its end.
			 */
			bool fallbackSafe = false;
		};

		struct Attribute {
			size_t location = 0;

			/** Index into `bindings`. */
			size_t binding = 0;

			VertexFormat format = VertexFormat::Invalid;
			size_t offset = 0;
		};

		std::vector<Binding> bindings;
		std::vector<Attribute> attributes;
	};

	/** The size in bytes of one attribute of `format`; 0 for Invalid or an unknown value. */
	static inline size_t vertexFormatSize(VertexFormat format) {
		switch (format) {
			case VertexFormat::UChar:
			case VertexFormat::Char:
			case VertexFormat::UCharNormalized:
			case VertexFormat::CharNormalized:
				return 1;
			case VertexFormat::UChar2:
			case VertexFormat::Char2:
			case VertexFormat::UChar2Normalized:
			case VertexFormat::Char2Normalized:
			case VertexFormat::UShort:
			case VertexFormat::Short:
			case VertexFormat::UShortNormalized:
			case VertexFormat::ShortNormalized:
			case VertexFormat::Half:
				return 2;
			case VertexFormat::UChar3:
			case VertexFormat::Char3:
			case VertexFormat::UChar3Normalized:
			case VertexFormat::Char3Normalized:
				return 3;
			case VertexFormat::UChar4:
			case VertexFormat::Char4:
			case VertexFormat::UChar4Normalized:
			case VertexFormat::Char4Normalized:
			case VertexFormat::UChar4Normalized_BGRA:
			case VertexFormat::UShort2:
			case VertexFormat::Short2:
			case VertexFormat::UShort2Normalized:
			case VertexFormat::Short2Normalized:
			case VertexFormat::Half2:
			case VertexFormat::Float:
			case VertexFormat::Int:
			case VertexFormat::UInt:
			case VertexFormat::Int1010102Normalized:
			case VertexFormat::UInt1010102Normalized:
				return 4;
			case VertexFormat::UShort3:
			case VertexFormat::Short3:
			case VertexFormat::UShort3Normalized:
			case VertexFormat::Short3Normalized:
			case VertexFormat::Half3:
				return 6;
			case VertexFormat::UShort4:
			case VertexFormat::Short4:
			case VertexFormat::UShort4Normalized:
			case VertexFormat::Short4Normalized:
			case VertexFormat::Half4:
			case VertexFormat::Float2:
			case VertexFormat::Int2:
			case VertexFormat::UInt2:
				return 8;
			case VertexFormat::Float3:
			case VertexFormat::Int3:
			case VertexFormat::UInt3:
				return 12;
			case VertexFormat::Float4:
			case VertexFormat::Int4:
			case VertexFormat::UInt4:
				return 16;
			default:
				return 0;
		}
	}

	/**
	 * Builds the vertex input state from the descriptor.
	 *
	 * Only attributes at `consumedLocations`, the locations the vertex function reads, are given to
	 * Vulkan, and only the buffer layouts those attributes use. A descriptor commonly describes more
	 * than a shader reads, and the layouts of the unread attributes are buffers the application need
	 * not bind. Without `consumedLocations` (a function whose reflection lists no vertex inputs)
	 * every described attribute is kept.
	 *
	 * A constant step function (MTLVertexStepFunctionConstant) fetches one element for every vertex
	 * and instance, whatever its stride and step rate: a Vulkan binding of stride zero.
	 *
	 * @param fallbackLength The size of the device's zero-filled fallback buffer.
	 * @throws std::runtime_error An attribute names a buffer layout the descriptor lacks, or a layout
	 *         uses a step function or step rate that is not supported.
	 */
	static inline VertexInputPlan planVertexInput(const VertexDescriptor& descriptor, const std::optional<std::unordered_set<size_t>>& consumedLocations, size_t fallbackLength) {
		VertexInputPlan plan;

		std::vector<size_t> locations;
		for (const auto& [location, attribute]: descriptor.attributes) {
			if (attribute.format == VertexFormat::Invalid) {
				continue;
			}
			if (consumedLocations && !consumedLocations->count(location)) {
				continue;
			}
			locations.push_back(location);
		}
		std::sort(locations.begin(), locations.end());

		std::vector<size_t> bufferIndices;
		for (auto location: locations) {
			bufferIndices.push_back(descriptor.attributes.at(location).bufferIndex);
		}
		std::sort(bufferIndices.begin(), bufferIndices.end());
		bufferIndices.erase(std::unique(bufferIndices.begin(), bufferIndices.end()), bufferIndices.end());

		for (auto metalIndex: bufferIndices) {
			auto found = descriptor.layouts.find(metalIndex);
			if (found == descriptor.layouts.end()) {
				throw std::runtime_error("A vertex attribute uses buffer " + std::to_string(metalIndex) + ", which has no layout");
			}
			const auto& layout = found->second;

			VertexInputPlan::Binding binding;
			binding.metalIndex = metalIndex;
			if (layout.stepFunction == VertexStepFunction::Constant) {
				binding.stride = 0;
			} else {
				if (layout.stepRate != 1) {
					throw std::runtime_error("Vertex step rates other than 1 are not supported");
				}
				if (layout.stepFunction != VertexStepFunction::PerVertex && layout.stepFunction != VertexStepFunction::PerInstance) {
					throw std::runtime_error("Only the constant, per-vertex and per-instance vertex step functions are supported");
				}
				binding.stride = layout.stride;
				binding.perInstance = layout.stepFunction == VertexStepFunction::PerInstance;
			}
			binding.fallbackSafe = binding.stride == 0;
			plan.bindings.push_back(binding);
		}

		for (auto location: locations) {
			const auto& attribute = descriptor.attributes.at(location);
			auto binding = static_cast<size_t>(std::lower_bound(bufferIndices.begin(), bufferIndices.end(), attribute.bufferIndex) - bufferIndices.begin());

			// subtract only after comparing the offset: offset + size could overflow
			auto size = vertexFormatSize(attribute.format);
			if (attribute.offset > fallbackLength || size > fallbackLength - attribute.offset) {
				plan.bindings[binding].fallbackSafe = false;
			}

			plan.attributes.push_back(VertexInputPlan::Attribute { location, binding, attribute.format, attribute.offset });
		}

		return plan;
	}
};
