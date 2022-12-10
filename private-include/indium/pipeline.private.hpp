#pragma once

#include <indium/library.private.hpp>
#include <indium/device.private.hpp>

namespace Indium {
	template<size_t count>
	struct DescriptorSetLayouts {
	private:
		INDIUM_PREVENT_COPY(DescriptorSetLayouts);

	public:
		std::array<VkDescriptorSetLayout, count> layouts;
		std::shared_ptr<PrivateDevice> privateDevice;

		DescriptorSetLayouts(std::shared_ptr<PrivateDevice> device):
			privateDevice(device)
		{
			for (size_t i = 0; i < layouts.size(); ++i) {
				layouts[i] = nullptr;
			}
		};

		~DescriptorSetLayouts() {
			for (size_t i = 0; i < layouts.size(); ++i) {
				if (layouts[i] == nullptr) {
					continue;
				}
				vkDestroyDescriptorSetLayout(privateDevice->device(), layouts[i], nullptr);
			}
		};

		void processFunction(std::shared_ptr<PrivateFunction> function, size_t layoutIndex) {
			size_t index = 0;
			std::vector<VkDescriptorSetLayoutBinding> bindings;

			bool needUBO = false;

			for (const auto bindingInfo: function->functionInfo().bindings) {
				if (bindingInfo.type == Iridium::BindingType::Buffer) {
					needUBO = true;
				} else if (bindingInfo.type == Iridium::BindingType::Texture) {
					auto& binding = bindings.emplace_back();
					binding.binding = bindingInfo.internalIndex;
					binding.descriptorType = (bindingInfo.textureAccessType == Iridium::TextureAccessType::Sample) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					binding.descriptorCount = 1;
					binding.stageFlags = functionTypeToVkShaderStageFlags(function->functionInfo().functionType);
				} else if (bindingInfo.type == Iridium::BindingType::Sampler) {
					auto& binding = bindings.emplace_back();
					binding.binding = bindingInfo.internalIndex;
					binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					binding.descriptorCount = 1;
					binding.stageFlags = functionTypeToVkShaderStageFlags(function->functionInfo().functionType);
				}
			}

			if (needUBO) {
				// we need a UBO to store the buffer addresses
				auto& binding = bindings.emplace_back();
				binding.binding = 0;
				binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				binding.descriptorCount = 1;
				binding.stageFlags = functionTypeToVkShaderStageFlags(function->functionInfo().functionType);
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo {};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = bindings.size();
			layoutInfo.pBindings = bindings.data();

			VkDescriptorSetLayout layout;

			if (vkCreateDescriptorSetLayout(privateDevice->device(), &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
				// TODO
				abort();
			}

			layouts[layoutIndex] = layout;
		};
	};
};
