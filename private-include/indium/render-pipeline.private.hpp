#pragma once

#include <indium/render-pipeline.hpp>

#include <vulkan/vulkan.h>

#include <array>

namespace Indium {
	class PrivateDevice;
	class PrivateFunction;
	struct FunctionInfo;

	class PrivateRenderPipelineState: public RenderPipelineState {
		private:
			std::vector<RenderPipelineColorAttachmentDescriptor> _colorAttachments;
			PrimitiveTopologyClass _primitiveTopology;
			std::shared_ptr<PrivateFunction> _vertexFunction;
			std::shared_ptr<PrivateFunction> _fragmentFunction;
			std::optional<VertexDescriptor> _vertexDescriptor;

		public:
			PrivateRenderPipelineState(std::shared_ptr<PrivateDevice> device, const RenderPipelineDescriptor& descriptor);
			~PrivateRenderPipelineState();

			virtual std::shared_ptr<Device> device() override;

			// TODO: can the same render pipeline state be used in multiple render encoders simultaneously?
			//       it shouldn't really matter since the render pipeline state must be compatible with the render encoder,
			//       except that we would have to avoid race conditions when recreating the pipeline.
			//
			// TODO: see if we can take advantage of Vulkan render pass compatibility
			//       and create a dummy render pass using the information we're given
			//       (which does not include e.g. load and store ops).
			//       if so, we would always be able to create the pipeline immediately
			//       in the constructor.
			void recreatePipeline(VkRenderPass compatibleRenderPass, bool force);

			const FunctionInfo& vertexFunctionInfo();
			const FunctionInfo& fragmentFunctionInfo();

			INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);

			// one for each topology class
			using PipelineArray = std::array<VkPipeline, 3>;
			INDIUM_PROPERTY(PipelineArray, p, P,ipelines) {};
			INDIUM_PROPERTY(VkPipelineLayout, p, P,ipelineLayout) = nullptr;

			using DescriptorSetLayoutArray = std::array<VkDescriptorSetLayout, 2>;
			INDIUM_PROPERTY(DescriptorSetLayoutArray, d, D,escriptorSetLayouts) {};

			INDIUM_PROPERTY_READONLY_REF(std::vector<size_t>, v,V,ertexInputBindings);
	};
};
