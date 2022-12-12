#include <indium/render-pipeline.private.hpp>
#include <indium/device.private.hpp>
#include <indium/library.private.hpp>
#include <indium/types.private.hpp>

#include <stdexcept>

Indium::RenderPipelineState::~RenderPipelineState() {};

std::shared_ptr<Indium::Device> Indium::PrivateRenderPipelineState::device() {
	return _privateDevice;
};

Indium::PrivateRenderPipelineState::PrivateRenderPipelineState(std::shared_ptr<PrivateDevice> device, const RenderPipelineDescriptor& descriptor):
	_privateDevice(device),
	_descriptorSetLayouts(_privateDevice)
{
	_colorAttachments = descriptor.colorAttachments;
	_vertexDescriptor = descriptor.vertexDescriptor;
	_primitiveTopology = descriptor.inputPrimitiveTopology;

	_vertexFunction = std::dynamic_pointer_cast<PrivateFunction>(descriptor.vertexFunction);
	_fragmentFunction = std::dynamic_pointer_cast<PrivateFunction>(descriptor.fragmentFunction);

	_descriptorSetLayouts.processFunction(_vertexFunction, 0);
	_descriptorSetLayouts.processFunction(_fragmentFunction, 1);
};

Indium::PrivateRenderPipelineState::~PrivateRenderPipelineState() {
	if (_pipelines[0]) {
		for (auto& pipeline: _pipelines) {
			vkDestroyPipeline(_privateDevice->device(), pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}
	}

	if (_pipelineLayout) {
		vkDestroyPipelineLayout(_privateDevice->device(), _pipelineLayout, nullptr);
		_pipelineLayout = VK_NULL_HANDLE;
	}
};

void Indium::PrivateRenderPipelineState::recreatePipeline(VkRenderPass compatibleRenderPass, bool force) {
	if (!force && _pipelines[0]) {
		// assumes that the current pipeline is compatible with the given render pass
		return;
	}

	if (_pipelines[0]) {
		for (auto& pipeline: _pipelines) {
			vkDestroyPipeline(_privateDevice->device(), pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}
	}

	if (_pipelineLayout) {
		vkDestroyPipelineLayout(_privateDevice->device(), _pipelineLayout, nullptr);
		_pipelineLayout = VK_NULL_HANDLE;
	}

	auto vertexFunctionName = _vertexFunction->name();
	auto fragmentFunctionName = _fragmentFunction->name();
	std::vector<VkPipelineShaderStageCreateInfo> stages;

	VkPipelineShaderStageCreateInfo shaderStage {};
	shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

	shaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStage.module = _vertexFunction->library()->shaderModule();
	shaderStage.pName = vertexFunctionName.c_str();
	stages.push_back(shaderStage);

	shaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStage.module = _fragmentFunction->library()->shaderModule();
	shaderStage.pName = fragmentFunctionName.c_str();
	stages.push_back(shaderStage);

	VkPipelineVertexInputStateCreateInfo vertexInputState {};
	vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	std::vector<VkVertexInputBindingDescription> vertexBindingDescs;
	std::vector<VkVertexInputAttributeDescription> vertexAttrDescs;

	if (_vertexDescriptor) {
		const auto& desc = *_vertexDescriptor;

		// used to get rid of holes between binding indices (i.e. to compact them and make them easier to bind on our side)
		std::unordered_map<size_t, size_t> metalToVulkanBindingIndices;
		size_t currIndex = 0;

		for (const auto& [index, layout]: desc.layouts) {
			VkVertexInputBindingDescription bindingDesc {};

			if (layout.stepRate != 1) {
				throw std::runtime_error("TODO: support step rates other than 1");
			}
			if (layout.stepFunction != VertexStepFunction::PerVertex && layout.stepFunction != VertexStepFunction::PerInstance) {
				throw std::runtime_error("TODO: support step functions other than per-vertex and per-instance");
			}

			auto vulkanIndex = currIndex;
			metalToVulkanBindingIndices[index] = vulkanIndex;
			++currIndex;

			bindingDesc.binding = vulkanIndex;
			bindingDesc.stride = layout.stride;
			bindingDesc.inputRate = (layout.stepFunction == VertexStepFunction::PerVertex) ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;

			vertexBindingDescs.push_back(bindingDesc);
		}

		for (const auto& [index, attr]: desc.attributes) {
			VkVertexInputAttributeDescription attrDesc {};

			attrDesc.location = index;
			attrDesc.binding = metalToVulkanBindingIndices[attr.bufferIndex];
			attrDesc.format = vertexFormatToVkFormat(attr.format);
			attrDesc.offset = attr.offset;

			vertexAttrDescs.push_back(attrDesc);
		}

		_vertexInputBindings.resize(currIndex);

		for (const auto& [metal, vulkan]: metalToVulkanBindingIndices) {
			_vertexInputBindings[vulkan] = metal;
		}
	}

	vertexInputState.vertexBindingDescriptionCount = vertexBindingDescs.size();
	vertexInputState.pVertexBindingDescriptions = vertexBindingDescs.data();
	vertexInputState.vertexAttributeDescriptionCount = vertexAttrDescs.size();
	vertexInputState.pVertexAttributeDescriptions = vertexAttrDescs.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState {};
	inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

	VkPipelineTessellationStateCreateInfo tesselationState {};
	tesselationState.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;

	VkPipelineViewportStateCreateInfo viewportState {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

	VkPipelineRasterizationStateCreateInfo rasterizationState {};
	rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	// TODO: figure out how to set `depthClampEnable` and `polgygonMode` dynamically;
	//       might have to just create multiple pipelines
	rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationState.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampleState {};
	multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleState.minSampleShading = 1.0f;

	VkPipelineDepthStencilStateCreateInfo depthStencilState {};
	depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

	std::vector<VkPipelineColorBlendAttachmentState> colorBlendStates;
	for (const auto& colorInfo: _colorAttachments) {
		VkPipelineColorBlendAttachmentState tmp;
		tmp.blendEnable = colorInfo.blendingEnabled;
		tmp.srcColorBlendFactor = blendFactorToVkBlendFactor(colorInfo.sourceRGBBlendFactor);
		tmp.dstColorBlendFactor = blendFactorToVkBlendFactor(colorInfo.destinationRGBBlendFactor);
		tmp.colorBlendOp = blendOperationToVkBlendOp(colorInfo.rgbBlendOperation);
		tmp.srcAlphaBlendFactor = blendFactorToVkBlendFactor(colorInfo.sourceAlphaBlendFactor);
		tmp.dstAlphaBlendFactor = blendFactorToVkBlendFactor(colorInfo.destinationAlphaBlendFactor);
		tmp.alphaBlendOp = blendOperationToVkBlendOp(colorInfo.alphaBlendOperation);
		tmp.colorWriteMask = colorWriteMaskToVkColorComponentFlags(colorInfo.writeMask);
		colorBlendStates.push_back(tmp);
	}
	VkPipelineColorBlendStateCreateInfo colorBlendState {};
	colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendState.attachmentCount = colorBlendStates.size();
	colorBlendState.pAttachments = colorBlendStates.data();

	std::vector<VkDynamicState> dynamicStates {
		VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
		VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
		VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
		VK_DYNAMIC_STATE_CULL_MODE,
		VK_DYNAMIC_STATE_FRONT_FACE,

		VK_DYNAMIC_STATE_DEPTH_BIAS,
		VK_DYNAMIC_STATE_DEPTH_BOUNDS,
		VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
		VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,

		VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
		VK_DYNAMIC_STATE_STENCIL_OP,
		VK_DYNAMIC_STATE_STENCIL_REFERENCE,
		VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
		VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,

		VK_DYNAMIC_STATE_BLEND_CONSTANTS,
		VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,
	};
	VkPipelineDynamicStateCreateInfo dynamicState {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = dynamicStates.size();
	dynamicState.pDynamicStates = dynamicStates.data();

	std::vector<VkPushConstantRange> pushConstantRanges;

	VkPipelineLayoutCreateInfo layoutCreateInfo {};
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutCreateInfo.setLayoutCount = _descriptorSetLayouts.layouts.size();
	layoutCreateInfo.pSetLayouts = _descriptorSetLayouts.layouts.data();
	layoutCreateInfo.pushConstantRangeCount = pushConstantRanges.size();
	layoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();

	if (vkCreatePipelineLayout(_privateDevice->device(), &layoutCreateInfo, nullptr, &_pipelineLayout) != VK_SUCCESS) {
		// TODO
		abort();
	}

	VkGraphicsPipelineCreateInfo pipelineCreateInfo {};
	pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCreateInfo.stageCount = stages.size();
	pipelineCreateInfo.pStages = stages.data();
	pipelineCreateInfo.pVertexInputState = &vertexInputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pTessellationState = &tesselationState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pDynamicState = &dynamicState;
	pipelineCreateInfo.layout = _pipelineLayout;
	pipelineCreateInfo.renderPass = compatibleRenderPass;

	// we can set the primitive topology dynamically, but unfortunately, we can only set it for a different topology
	// in the same topology class that the pipeline was created for. this means we need to create 3 pipelines
	// (one for each topology class) rather than 5 (one for each primitive topology).

	// first, one for points
	inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	if (vkCreateGraphicsPipelines(_privateDevice->device(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &_pipelines[0]) != VK_SUCCESS) {
		// TODO
		abort();
	}

	// now, one for lines
	inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	if (vkCreateGraphicsPipelines(_privateDevice->device(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &_pipelines[1]) != VK_SUCCESS) {
		// TODO
		abort();
	}

	// finally, one for triangles
	inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	if (vkCreateGraphicsPipelines(_privateDevice->device(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &_pipelines[2]) != VK_SUCCESS) {
		// TODO
		abort();
	}
};

const Indium::FunctionInfo& Indium::PrivateRenderPipelineState::vertexFunctionInfo() {
	return _vertexFunction->functionInfo();
};

const Indium::FunctionInfo& Indium::PrivateRenderPipelineState::fragmentFunctionInfo() {
	return _fragmentFunction->functionInfo();
};
