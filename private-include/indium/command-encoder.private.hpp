#pragma once

#include <vector>
#include <memory>
#include <optional>
#include <array>
#include <forward_list>

#include <indium/command-encoder.hpp>
#include <indium/device.private.hpp>
#include <indium/buffer.hpp>
#include <indium/sampler.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/library.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <iridium/iridium.hpp>

namespace Indium {
	struct FunctionResources {
		std::vector<std::pair<std::shared_ptr<Buffer>, size_t>> buffers;
		std::vector<std::shared_ptr<Texture>> textures;
		std::vector<std::shared_ptr<SamplerState>> samplers;

		void setBytes(std::shared_ptr<Device> device, const void* bytes, size_t length, size_t index) {
			// TODO: we can make this "Private" instead
			auto buf = device->newBuffer(bytes, length, ResourceOptions::StorageModeShared);

			if (buffers.size() <= index) {
				buffers.resize(index + 1);
			}

			buffers[index] = std::make_pair(buf, 0);
		};

		void setBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) {
			if (buffers.size() <= index) {
				buffers.resize(index + 1);
			}

			buffers[index] = std::make_pair(buffer, offset);
		};

		void setBufferOffset(size_t offset, size_t index) {
			buffers[index].second = offset;
		};

		void setSamplerState(std::shared_ptr<SamplerState> state, std::optional<std::pair<float, float>> lodClamps, size_t index) {
			if (samplers.size() <= index) {
				samplers.resize(index + 1);
			}

			if (lodClamps) {
				auto privateState = std::dynamic_pointer_cast<PrivateSamplerState>(state);
				samplers[index] = privateState->cloneWithClamps(lodClamps->first, lodClamps->second);
			} else {
				samplers[index] = state;
			}
		};

		void setTexture(std::shared_ptr<Texture> texture, size_t index) {
			if (textures.size() <= index) {
				textures.resize(index + 1);
			}

			textures[index] = texture;
		};
	};

	template<size_t setCount>
	std::array<VkDescriptorSet, setCount> createDescriptorSets(std::array<VkDescriptorSetLayout, setCount>& setLayouts, VkDescriptorPool pool, std::shared_ptr<PrivateDevice> privateDevice, const std::array<std::reference_wrapper<const FunctionResources>, setCount>& funcResources, const std::array<std::reference_wrapper<const FunctionInfo>, setCount>& functionInfos, std::vector<std::shared_ptr<Buffer>>& keepAliveBuffers) {
		std::array<VkDescriptorSet, setCount> descriptorSets {};

		VkDescriptorSetAllocateInfo setAllocateInfo {};
		setAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		setAllocateInfo.descriptorPool = pool;
		setAllocateInfo.descriptorSetCount = descriptorSets.size();
		setAllocateInfo.pSetLayouts = setLayouts.data();

		if (DynamicVK::vkAllocateDescriptorSets(privateDevice->device(), &setAllocateInfo, descriptorSets.data()) != VK_SUCCESS) {
			// TODO
			abort();
		}

		for (size_t i = 0; i < descriptorSets.size(); ++i) {
			std::vector<VkWriteDescriptorSet> writeDescSet;
			std::forward_list<VkDescriptorBufferInfo> bufInfos;
			std::forward_list<VkDescriptorImageInfo> imageInfos;

			const FunctionResources& functionResources = funcResources[i];
			const FunctionInfo& funcInfo = functionInfos[i];

			if (functionResources.buffers.size() > 0) {
				std::vector<uint64_t> addresses;

				// find the right buffer for each binding (using the binding index)
				for (size_t j = 0; j < funcInfo.bindings.size(); ++j) {
					auto& bindingInfo = funcInfo.bindings[j];

					if (bindingInfo.type != Iridium::BindingType::Buffer) {
						continue;
					}

					if (bindingInfo.index >= functionResources.buffers.size()) {
						addresses.push_back(0);
						continue;
					}

					auto privateBuf = std::dynamic_pointer_cast<PrivateBuffer>(functionResources.buffers[bindingInfo.index].first);
					addresses.push_back(privateBuf->gpuAddress() + functionResources.buffers[bindingInfo.index].second);
				}

				auto addressBuffer = privateDevice->newBuffer(addresses.data(), functionResources.buffers.size() * 8, ResourceOptions::StorageModeShared);
				auto privateAddrBuf = std::dynamic_pointer_cast<PrivateBuffer>(addressBuffer);

				// we need to keep this buffer alive until the operation is completed
				keepAliveBuffers.push_back(addressBuffer);

				auto& info = bufInfos.emplace_front();
				info.buffer = privateAddrBuf->buffer();
				info.offset = 0;
				info.range = VK_WHOLE_SIZE;

				auto& descSet = writeDescSet.emplace_back();
				descSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descSet.dstSet = descriptorSets[i];
				descSet.dstBinding = 0;
				descSet.dstArrayElement = 0;
				descSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				descSet.descriptorCount = 1;
				descSet.pBufferInfo = &info;
			}

			for (size_t j = 0; j < funcInfo.bindings.size(); ++j) {
				auto& bindingInfo = funcInfo.bindings[j];

				if (bindingInfo.type == Iridium::BindingType::Texture) {
					if (bindingInfo.index >= functionResources.textures.size()) {
						continue;
					}

					auto texture = functionResources.textures[bindingInfo.index];
					auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);

					auto& info = imageInfos.emplace_front();
					info.imageView = privateTexture->imageView();
					info.imageLayout = privateTexture->imageLayout();

					auto& descSet = writeDescSet.emplace_back();
					descSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					descSet.dstSet = descriptorSets[i];
					descSet.dstBinding = bindingInfo.internalIndex;
					descSet.dstArrayElement = 0;
					descSet.descriptorType = (bindingInfo.textureAccessType == Iridium::TextureAccessType::Sample) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					descSet.descriptorCount = 1;
					descSet.pImageInfo = &info;
				} else if (bindingInfo.type == Iridium::BindingType::Sampler) {
					bool embeddedSampler = false;

					if (bindingInfo.index == SIZE_MAX) {
						// this binding uses an embedded sampler
						embeddedSampler = true;
					} else if (bindingInfo.index >= functionResources.samplers.size()) {
						continue;
					}

					auto sampler = embeddedSampler ? funcInfo.embeddedSamplerStates[bindingInfo.embeddedSamplerIndex] : functionResources.samplers[bindingInfo.index];
					auto privateSampler = std::dynamic_pointer_cast<PrivateSamplerState>(sampler);

					auto& info = imageInfos.emplace_front();
					info.sampler = privateSampler->sampler();

					auto& descSet = writeDescSet.emplace_back();
					descSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					descSet.dstSet = descriptorSets[i];
					descSet.dstBinding = bindingInfo.internalIndex;
					descSet.dstArrayElement = 0;
					descSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					descSet.descriptorCount = 1;
					descSet.pImageInfo = &info;
				}
			}

			DynamicVK::vkUpdateDescriptorSets(privateDevice->device(), writeDescSet.size(), writeDescSet.data(), 0, nullptr);
		}

		return descriptorSets;
	};

	static constexpr std::array<VkDescriptorPoolSize, 4> poolSizes {
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLER, 512 },
	};
};
