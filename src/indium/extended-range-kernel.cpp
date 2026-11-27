// Copyright (C) 2026 Lubos Dolezel
#include <indium/extended-range-kernel.private.hpp>
#include <indium/device.private.hpp>

#include "extended-range-spv.hpp"

#include <stdexcept>

Indium::ExtendedRangeKernel::ExtendedRangeKernel(PrivateDevice& device):
	_device(device.device())
{
	try {
		VkDescriptorSetLayoutBinding binding {};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo setLayoutInfo {};
		setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		setLayoutInfo.bindingCount = 1;
		setLayoutInfo.pBindings = &binding;
		if (DynamicVK::vkCreateDescriptorSetLayout(_device, &setLayoutInfo, nullptr, &_setLayout) != VK_SUCCESS) {
			throw std::runtime_error("Cannot create the descriptor set layout of the extended-range kernel");
		}

		VkPushConstantRange pushRange {};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(uint32_t);

		VkPipelineLayoutCreateInfo layoutInfo {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &_setLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;
		if (DynamicVK::vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS) {
			throw std::runtime_error("Cannot create the pipeline layout of the extended-range kernel");
		}

		VkShaderModuleCreateInfo moduleInfo {};
		moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleInfo.codeSize = sizeof(extendedRangeSpirv);
		moduleInfo.pCode = extendedRangeSpirv;
		if (DynamicVK::vkCreateShaderModule(_device, &moduleInfo, nullptr, &_module) != VK_SUCCESS) {
			throw std::runtime_error("Cannot create the shader module of the extended-range kernel");
		}

		VkComputePipelineCreateInfo pipelineInfo {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pipelineInfo.stage.module = _module;
		pipelineInfo.stage.pName = "main";
		pipelineInfo.layout = _pipelineLayout;
		if (device.createComputePipeline(pipelineInfo, &_pipeline) != VK_SUCCESS) {
			throw std::runtime_error("Cannot create the pipeline of the extended-range kernel");
		}
	} catch (...) {
		// the destructor does not run for a constructor that throws
		DynamicVK::vkDestroyPipeline(_device, _pipeline, nullptr);
		DynamicVK::vkDestroyShaderModule(_device, _module, nullptr);
		DynamicVK::vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
		DynamicVK::vkDestroyDescriptorSetLayout(_device, _setLayout, nullptr);
		throw;
	}
};

Indium::ExtendedRangeKernel::~ExtendedRangeKernel() {
	DynamicVK::vkDestroyPipeline(_device, _pipeline, nullptr);
	DynamicVK::vkDestroyShaderModule(_device, _module, nullptr);
	DynamicVK::vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
	DynamicVK::vkDestroyDescriptorSetLayout(_device, _setLayout, nullptr);
};

void Indium::ExtendedRangeKernel::record(VkCommandBuffer commandBuffer, VkDescriptorSet set, VkImageView view, uint32_t width, uint32_t height, Mode mode) {
	VkDescriptorImageInfo imageInfo {};
	imageInfo.imageView = view;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet write {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = set;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	write.pImageInfo = &imageInfo;
	DynamicVK::vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);

	uint32_t modeValue = static_cast<uint32_t>(mode);
	DynamicVK::vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
	DynamicVK::vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &set, 0, nullptr);
	DynamicVK::vkCmdPushConstants(commandBuffer, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(modeValue), &modeValue);
	DynamicVK::vkCmdDispatch(commandBuffer, (width + 7) / 8, (height + 7) / 8, 1);
};
