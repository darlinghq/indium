#pragma once

#include <indium/compute-command-encoder.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/command-encoder.private.hpp>
#include <indium/compute-pipeline.private.hpp>

namespace Indium {
	class PrivateComputeCommandEncoder: public ComputeCommandEncoder {
	private:
		// the command buffer always outlives us
		std::weak_ptr<PrivateCommandBuffer> _privateCommandBuffer;
		std::shared_ptr<PrivateDevice> _privateDevice;
		ComputePassDescriptor _descriptor;
		FunctionResources _functionResources;
		std::shared_ptr<PrivateComputePipelineState> _pso;
		VkDescriptorPool _pool = VK_NULL_HANDLE;

		// XXX: not sure if we actually need to store pipelines until the command buffer finishes executing,
		//      but let's do it just in case.
		std::vector<VkPipeline> _savedPipelines;
		std::vector<FunctionResources> _savedFunctionResources;
		std::vector<std::shared_ptr<Buffer>> _keepAliveBuffers;

		void updateBindings();

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

		virtual void dispatchThreadgroups(Size threadgroupsPerGrid, Size threadsPerThreadgroup) override;
		virtual void dispatchThreads(Size threadsPerGrid, Size threadsPerThreadgroup) override;

		virtual void setImageblockSize(size_t width, size_t height) override;

		virtual void setStageInRegion(Region region) override;

		virtual DispatchType dispatchType() override;

		virtual void endEncoding() override;
	};
};
