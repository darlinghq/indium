#pragma once

#include <indium/types.hpp>
#include <indium/pipeline.hpp>
#include <indium/linked-functions.hpp>
#include <indium/binary-archive.hpp>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Indium {
	class Device;
	class Function;
	class DynamicLibrary;
	class FunctionHandle;
	class VisibleFunctionTable;
	struct VisibleFunctionTableDescriptor;
	class IntersectionFunctionTable;
	struct IntersectionFunctionTableDescriptor;

	struct AttributeDescriptor {
		size_t bufferIndex = 0;
		size_t offset = 0;
		AttributeFormat format = AttributeFormat::Invalid;
	};

	struct BufferLayoutDescriptor {
		size_t stride = 0;
		StepFunction stepFunction = StepFunction::PerInstance; // XXX: not sure what the actual default is
		size_t stepRate = 1;
	};

	struct StageInputOutputDescriptor {
		std::unordered_map<size_t, AttributeDescriptor> attributes;
		std::unordered_map<size_t, BufferLayoutDescriptor> layouts;
		size_t indexBufferIndex = 0;
		IndexType indexType = IndexType::UInt16;
	};

	struct ComputePipelineDescriptor {
		std::shared_ptr<Function> computeFunction;
		bool threadGroupSizeIsMultipleOfThreadExecutionWidth = false;
		size_t maxTotalThreadsPerThreadgroup = 0;
		size_t maxCallStackDepth = 1;
		std::optional<StageInputOutputDescriptor> stageInputDescriptor = std::nullopt;
		std::unordered_map<size_t, PipelineBufferDescriptor> buffers;
		bool supportIndirectCommandBuffers = false; // XXX: not sure what the actual default is
		std::vector<std::shared_ptr<DynamicLibrary>> preloadedLibraries;
		std::shared_ptr<LinkedFunctions> linkedFunctions;
		bool supportAddingBinaryFunctions = false; // XXX: not sure what the actual default is
		std::vector<std::shared_ptr<BinaryArchive>> binaryArchives;

		void reset() {
			*this = ComputePipelineDescriptor();
		};
	};

	class ComputePipelineState {
	public:
		virtual ~ComputePipelineState() = 0;

		virtual std::shared_ptr<Device> device() = 0;

		virtual size_t imageblockMemoryLength(Size dimensions) = 0;
		virtual std::shared_ptr<FunctionHandle> functionHandle(std::shared_ptr<Function> function) = 0;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(const std::vector<std::shared_ptr<Function>>& functions) = 0;
		virtual std::shared_ptr<VisibleFunctionTable> newVisibleFunctionTable(const VisibleFunctionTableDescriptor& descriptor) = 0;
		virtual std::shared_ptr<IntersectionFunctionTable> newIntersectionFunctionTable(const IntersectionFunctionTableDescriptor& descriptor) = 0;

		INDIUM_PROPERTY_READONLY(size_t, m,M,axTotalThreadsPerThreadgroup) = 0;
		INDIUM_PROPERTY_READONLY(size_t, t,T,hreadExecutionWidth) = 0;
		INDIUM_PROPERTY_READONLY(size_t, s,S,taticThreadgroupMemoryLength) = 0;
		INDIUM_PROPERTY_READONLY(bool, s,S,upportIndirectCommandBuffers) = false;
	};
};
