#pragma once

#include <indium/render-pipeline.hpp>
#include <indium/pipeline.private.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace Indium {
	class PrivateDevice;
	class PrivateFunction;
	struct FunctionInfo;

	class PrivateRenderPipelineState: public RenderPipelineState {
		private:
			std::vector<RenderPipelineColorAttachmentDescriptor> _colorAttachments;
			PrimitiveTopologyClass _primitiveTopology;
			VkSampleCountFlagBits _rasterSampleCount;
			std::shared_ptr<PrivateFunction> _vertexFunction;
			std::shared_ptr<PrivateFunction> _fragmentFunction;
			std::optional<VertexDescriptor> _vertexDescriptor;

			std::vector<VkVertexInputBindingDescription> _vertexBindingDescs;
			std::vector<VkVertexInputAttributeDescription> _vertexAttrDescs;

			// Everything vkCreateGraphicsPipelines needs, kept so that the variants for the other fill and
			// depth clip modes can be created when a draw first asks for them.
			struct PipelineRecipe;
			using PipelineArray = std::array<VkPipeline, 3>;

			// A pipeline is only valid in render passes that are compatible with the one it was created for, and
			// render pass compatibility includes the input attachment references (Vulkan, "Render Pass
			// Compatibility"). A pass either has them (it is one a fragment function that reads color attachments
			// runs in, see PrivateRenderCommandEncoder::enableInputAttachments()) or has none, so there are two
			// classes of passes, and the pipelines of each are made for a pass of that class, when it is first used.
			struct PassClass {
				std::shared_ptr<PipelineRecipe> recipe;
				PipelineArray pipelines {};
				std::map<uint32_t, VkPipeline> variants;
			};
			std::array<PassClass, 2> _passClasses;
			std::mutex _mutex;

			void destroyVariantsLocked(PassClass& passClass);

		public:
			PrivateRenderPipelineState(std::shared_ptr<PrivateDevice> device, const RenderPipelineDescriptor& descriptor);
			~PrivateRenderPipelineState();

			virtual std::shared_ptr<Device> device() override;

			// TODO: see if we can take advantage of Vulkan render pass compatibility
			//       and create a dummy render pass using the information we're given
			//       (which does not include e.g. load and store ops).
			//       if so, we would always be able to create the pipeline immediately
			//       in the constructor.
			void recreatePipeline(VkRenderPass compatibleRenderPass, bool force, bool inputAttachments = false);

			/**
			 * The pipeline for a topology class (0 points, 1 lines, 2 triangles) with the given per-draw
			 * rasterization state: `lines` rasterizes triangles as lines (it only means something for the
			 * triangle class) and `clamp` clamps depth to the viewport range instead of clipping. The pipelines
			 * for the default state exist from the start; the others are created the first time they are asked
			 * for. A device that lacks the Vulkan feature behind a mode ignores it.
			 */
			VkPipeline pipelineFor(size_t topologyClass, bool lines, bool clamp, bool inputAttachments = false);

			/** "render <vertex function> <fragment function>", the name pipeline timing reports. */
			std::string timingName() const;

			const FunctionInfo& vertexFunctionInfo();
			const FunctionInfo& fragmentFunctionInfo();

			INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);

			// one for each topology class
			INDIUM_PROPERTY(VkPipelineLayout, p, P,ipelineLayout) = VK_NULL_HANDLE;

			INDIUM_PROPERTY_REF(DescriptorSetLayouts<2>, d, D,escriptorSetLayouts);

			// the Metal buffer index of each vertex buffer binding, in binding order
			INDIUM_PROPERTY_READONLY_REF(std::vector<size_t>, v,V,ertexInputBindings);

			// for each binding: whether the device's zero-filled buffer can stand in for it when the
			// application binds nothing (see VertexInputPlan::Binding::fallbackSafe)
			INDIUM_PROPERTY_READONLY_REF(std::vector<bool>, v,V,ertexInputFallbackSafe);
	};
};
