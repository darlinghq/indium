#pragma once

#include <indium/command-encoder.hpp>
#include <indium/event.hpp>
#include <indium/counter.hpp>
#include <indium/types.hpp>
#include <indium/compute-pipeline.hpp>

#include <memory>
#include <unordered_map>

namespace Indium {
	class Buffer;
	class SamplerState;
	class Texture;

	struct ComputePassSampleBufferAttachmentDescriptor {
		std::shared_ptr<CounterSampleBuffer> sampleBuffer;
		size_t startOfEncoderSampleIndex;
		size_t endOfEncoderSampleIndex;
	};

	struct ComputePassDescriptor {
		std::unordered_map<size_t, ComputePassSampleBufferAttachmentDescriptor> sampleBufferAttachments;
		DispatchType dispatchType = DispatchType::Serial;
	};

	class ComputeCommandEncoder: public CommandEncoder {
	public:
		virtual ~ComputeCommandEncoder() = 0;

		virtual void setComputePipelineState(std::shared_ptr<ComputePipelineState> state) = 0;

		virtual void setBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) = 0;
		virtual void setBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) = 0;
		virtual void setBufferOffset(size_t offset, size_t index) = 0;
		virtual void setBytes(const void* bytes, size_t length, size_t index) = 0;

		virtual void setSamplerState(std::shared_ptr<SamplerState> state, size_t index) = 0;
		virtual void setSamplerState(std::shared_ptr<SamplerState> state, float lodMinClamp, float lodMaxClamp, size_t index) = 0;
		virtual void setSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) = 0;
		virtual void setSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) = 0;

		virtual void setTexture(std::shared_ptr<Texture> texture, size_t index) = 0;
		virtual void setTextures(const std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) = 0;

		virtual void setThreadgroupMemoryLength(size_t length, size_t index) = 0;

		virtual void dispatchThreadgroups(Size threadgroupsPerGrid, Size threadsPerThreadgroup) = 0;
		virtual void dispatchThreads(Size threadsPerGrid, Size threadsPerThreadgroup) = 0;

		/**
		 * Dispatches the number of threadgroups the GPU reads from `indirectBuffer` at `indirectBufferOffset`:
		 * three 32-bit unsigned integers (width, height, depth). `indirectBufferOffset` must be a multiple of
		 * 4 and the arguments must lie inside the buffer; a call that does not satisfy that dispatches nothing.
		 */
		virtual void dispatchThreadgroups(const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset, Size threadsPerThreadgroup) = 0;

		virtual void setImageblockSize(size_t width, size_t height) = 0;

		virtual void setStageInRegion(Region region) = 0;

		virtual DispatchType dispatchType() = 0;

		/** See `BlitCommandEncoder::updateFence()`. */
		virtual void updateFence(const std::shared_ptr<Fence>& fence) = 0;
		/** See `BlitCommandEncoder::waitForFence()`. */
		virtual void waitForFence(const std::shared_ptr<Fence>& fence) = 0;
	};
};
