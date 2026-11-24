#pragma once

#include <indium/compute-command-encoder.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/command-encoder.private.hpp>
#include <indium/compute-pipeline.private.hpp>

#include <optional>
#include <unordered_map>

namespace Indium {
	class PipelineTimer;

	class PrivateComputeCommandEncoder: public ComputeCommandEncoder {
	private:
		// the command buffer always outlives us
		std::weak_ptr<PrivateCommandBuffer> _privateCommandBuffer;
		std::shared_ptr<PrivateDevice> _privateDevice;
		ComputePassDescriptor _descriptor;
		FunctionResources _functionResources;
		std::shared_ptr<PrivateComputePipelineState> _pso;
		// made when the first dispatch is encoded with pipeline timing on (Device::setPipelineTimingEnabled())
		std::shared_ptr<PipelineTimer> _timer;
		void beginTimedDispatch(VkCommandBuffer commandBuffer);
		void endTimedDispatch(VkCommandBuffer commandBuffer);
		DescriptorPoolChain _descriptorPools;

		// XXX: not sure if we actually need to store pipelines until the command buffer finishes executing,
		//      but let's do it just in case.
		std::vector<VkPipeline> _savedPipelines;
		std::vector<FunctionResources> _savedFunctionResources;
		std::vector<std::shared_ptr<Buffer>> _keepAliveBuffers;
		ScratchArena _scratch;
		// the length asked for each threadgroup memory argument index (see setThreadgroupMemoryLength())
		std::unordered_map<size_t, size_t> _threadgroupMemoryLengths;
		// the region setStageInRegion() set; without one, an empty region at the origin
		Region _stageInRegion {};
		// whether the commands of the encoder are ordered by what they access (see HazardTracker)
		bool _tracked = true;
		// resources declared with useResource() and useHeap(), which every later dispatch may access
		HazardAccessSet _declared;
		size_t _dispatchCount = 0;
		// a dispatch or a declaration reached a resource that is not tracked
		bool _usesUntracked = false;

		void updateBindings();
		/**
		 * Records the barriers that make the dispatch about to be recorded safe. `indirectBuffer` is the buffer the
		 * dispatch reads its arguments from, if any.
		 */
		void orderDispatch(const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset);
		void recordUntrackedBarrier();

	public:
		PrivateComputeCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const ComputePassDescriptor& descriptor);
		~PrivateComputeCommandEncoder();

		virtual void setComputePipelineState(std::shared_ptr<ComputePipelineState> state) override;

		virtual void setBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) override;
		virtual void setBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) override;
		virtual void setBufferOffset(size_t offset, size_t index) override;
		virtual void setBytes(const void* bytes, size_t length, size_t index) override;

		virtual void setSamplerState(std::shared_ptr<SamplerState> state, size_t index) override;
		virtual void setSamplerState(std::shared_ptr<SamplerState> state, float lodMinClamp, float lodMaxClamp, size_t index) override;
		virtual void setSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) override;
		virtual void setSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) override;

		virtual void setTexture(std::shared_ptr<Texture> texture, size_t index) override;
		virtual void setTextures(const std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) override;

		virtual void setThreadgroupMemoryLength(size_t length, size_t index) override;

		virtual void sampleCounters(const std::shared_ptr<CounterSampleBuffer>& sampleBuffer, size_t sampleIndex, bool barrier) override;
		virtual void dispatchThreadgroups(Size threadgroupsPerGrid, Size threadsPerThreadgroup) override;
		void dispatchThreadgroups(Size threadgroupsPerGrid, Size threadsPerThreadgroup, const std::optional<Size>& gridSize);
		virtual void dispatchThreads(Size threadsPerGrid, Size threadsPerThreadgroup) override;
		virtual void dispatchThreadgroups(const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset, Size threadsPerThreadgroup) override;

		virtual void setImageblockSize(size_t width, size_t height) override;

		virtual void useResource(std::shared_ptr<Resource> resource, ResourceUsage usage) override;
		virtual void useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage) override;
		virtual void useHeap(const std::shared_ptr<Heap>& heap) override;
		virtual void useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps) override;
		virtual void memoryBarrier(BarrierScope scope) override;
		virtual void memoryBarrier(const std::vector<std::shared_ptr<Resource>>& resources) override;

		virtual void setStageInRegion(Region region) override;

		virtual DispatchType dispatchType() override;

		virtual void updateFence(const std::shared_ptr<Fence>& fence) override;
		virtual void waitForFence(const std::shared_ptr<Fence>& fence) override;

		virtual void endEncoding() override;
	};
};
