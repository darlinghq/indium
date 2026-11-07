#pragma once

#include <algorithm>
#include <stdexcept>
#include <vector>
#include <memory>
#include <optional>
#include <array>
#include <forward_list>
#include <cstring>

#include <indium/command-encoder.hpp>
#include <indium/device.private.hpp>
#include <indium/buffer.hpp>
#include <indium/sampler.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/library.private.hpp>
#include <indium/dynamic-vk.hpp>

namespace Indium {
	// Metal tracks hazards between the encoders of a command buffer: what one encoder writes,
	// the next one sees. Vulkan commands in one command buffer are not ordered that way, so
	// every encoder ends with a full memory dependency on everything recorded before it.
	inline void finishTrackedEncoder(VkCommandBuffer commandBuffer) {
		VkMemoryBarrier barrier {};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		DynamicVK::vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	/**
	 * Bump allocation of small, short-lived GPU data (setBytes constants and the per-draw buffer
	 * address tables) from shared 256 KiB chunks, instead of one buffer, and so one device memory
	 * allocation, per draw or dispatch. Every chunk goes into the encoder's keep-alive list, so it
	 * lives until the command buffer has completed; allocations never overlap, so data written for
	 * one draw is never overwritten by a later one.
	 */
	struct ScratchArena {
		static constexpr size_t chunkSize = 256 * 1024;
		// at least Vulkan's largest permitted minUniformBufferOffsetAlignment, so an allocation can
		// be the offset of a uniform buffer descriptor
		static constexpr size_t alignment = 256;
		// larger requests get a buffer of their own
		static constexpr size_t maxScratchAllocation = chunkSize / 8;

		std::shared_ptr<Buffer> chunk;
		size_t used = 0;

		std::pair<std::shared_ptr<Buffer>, size_t> allocate(const std::shared_ptr<Device>& device, const void* bytes, size_t length, std::vector<std::shared_ptr<Buffer>>& keepAlive) {
			if (length > maxScratchAllocation) {
				auto buffer = device->newBuffer(bytes, length, ResourceOptions::StorageModeShared);
				keepAlive.push_back(buffer);
				return { buffer, 0 };
			}

			size_t offset = (used + alignment - 1) & ~(alignment - 1);
			if (!chunk || offset + length > chunkSize) {
				chunk = device->newBuffer(chunkSize, ResourceOptions::StorageModeShared);
				keepAlive.push_back(chunk);
				offset = 0;
			}

			std::memcpy(static_cast<char*>(chunk->contents()) + offset, bytes, length);
			used = offset + length;
			return { chunk, offset };
		};
	};

	struct FunctionResources {
		std::vector<std::pair<std::shared_ptr<Buffer>, size_t>> buffers;
		std::vector<std::shared_ptr<Texture>> textures;
		std::vector<std::shared_ptr<SamplerState>> samplers;

		void setBytes(const std::shared_ptr<Device>& device, ScratchArena& scratch, std::vector<std::shared_ptr<Buffer>>& keepAlive, const void* bytes, size_t length, size_t index) {
			// Metal copies the bytes at the time of the call
			auto allocation = scratch.allocate(device, bytes, length, keepAlive);

			if (buffers.size() <= index) {
				buffers.resize(index + 1);
			}

			buffers[index] = std::move(allocation);
		};

		void setBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) {
			if (buffers.size() <= index) {
				buffers.resize(index + 1);
			}

			buffers[index] = std::make_pair(buffer, offset);
		};

		void setBufferOffset(size_t offset, size_t index) {
			// only meaningful for a buffer that is already bound
			if (index < buffers.size()) {
				buffers[index].second = offset;
			}
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

	static constexpr std::array<VkDescriptorPoolSize, 4> poolSizes {
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLER, 512 },
	};

	/**
	 * The descriptor pools of one encoder. An encoder allocates descriptor sets for every draw or
	 * dispatch, so a fixed-size pool runs out; this adds a pool whenever the current one is
	 * exhausted. The sets stay valid until the chain is destroyed with its encoder, which the
	 * command buffer keeps alive until it completes.
	 */
	class DescriptorPoolChain {
		INDIUM_PREVENT_COPY(DescriptorPoolChain);

	private:
		VkDevice _device;
		std::vector<VkDescriptorPool> _pools;

		void addPool() {
			VkDescriptorPoolCreateInfo createInfo {};
			createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			createInfo.poolSizeCount = poolSizes.size();
			createInfo.pPoolSizes = poolSizes.data();
			createInfo.maxSets = 64;

			VkDescriptorPool pool = VK_NULL_HANDLE;
			if (DynamicVK::vkCreateDescriptorPool(_device, &createInfo, nullptr, &pool) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create a descriptor pool");
			}
			_pools.push_back(pool);
		};

	public:
		explicit DescriptorPoolChain(VkDevice device): _device(device) {
			addPool();
		};

		~DescriptorPoolChain() {
			for (auto pool: _pools) {
				DynamicVK::vkDestroyDescriptorPool(_device, pool, nullptr);
			}
		};

		void allocate(const VkDescriptorSetLayout* layouts, uint32_t count, VkDescriptorSet* sets) {
			VkDescriptorSetAllocateInfo allocateInfo {};
			allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocateInfo.descriptorSetCount = count;
			allocateInfo.pSetLayouts = layouts;

			allocateInfo.descriptorPool = _pools.back();
			auto result = DynamicVK::vkAllocateDescriptorSets(_device, &allocateInfo, sets);
			if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
				addPool();
				allocateInfo.descriptorPool = _pools.back();
				result = DynamicVK::vkAllocateDescriptorSets(_device, &allocateInfo, sets);
			}
			if (result != VK_SUCCESS) {
				throw std::runtime_error("Failed to allocate descriptor sets");
			}
		};
	};

	template<size_t setCount>
	std::array<VkDescriptorSet, setCount> createDescriptorSets(std::array<VkDescriptorSetLayout, setCount>& setLayouts, DescriptorPoolChain& pools, std::shared_ptr<PrivateDevice> privateDevice, const std::array<std::reference_wrapper<const FunctionResources>, setCount>& funcResources, const std::array<std::reference_wrapper<const FunctionInfo>, setCount>& functionInfos, std::vector<std::shared_ptr<Buffer>>& keepAliveBuffers, ScratchArena& scratch) {
		std::array<VkDescriptorSet, setCount> descriptorSets {};

		pools.allocate(setLayouts.data(), descriptorSets.size(), descriptorSets.data());

		for (size_t i = 0; i < descriptorSets.size(); ++i) {
			std::vector<VkWriteDescriptorSet> writeDescSet;
			std::forward_list<VkDescriptorBufferInfo> bufInfos;
			std::forward_list<VkDescriptorImageInfo> imageInfos;

			const FunctionResources& functionResources = funcResources[i];
			const FunctionInfo& funcInfo = functionInfos[i];

			if (functionResources.buffers.size() > 0 && std::any_of(funcInfo.bindings.begin(), funcInfo.bindings.end(), [](const BindingDescriptor& binding) {
				return binding.type == BindingType::Buffer;
			})) {
				std::vector<uint64_t> addresses;

				// find the right buffer for each binding (using the binding index)
				for (size_t j = 0; j < funcInfo.bindings.size(); ++j) {
					auto& bindingInfo = funcInfo.bindings[j];

					if (bindingInfo.type != BindingType::Buffer) {
						continue;
					}

					std::shared_ptr<PrivateBuffer> privateBuf;
					if (bindingInfo.index < functionResources.buffers.size()) {
						privateBuf = std::dynamic_pointer_cast<PrivateBuffer>(functionResources.buffers[bindingInfo.index].first);
					}
					if (!privateBuf) {
						// the shader would dereference address 0
						auto fallback = std::dynamic_pointer_cast<PrivateBuffer>(privateDevice->fallbackBuffer());
						addresses.push_back(fallback->gpuAddress());
						continue;
					}
					addresses.push_back(privateBuf->gpuAddress() + functionResources.buffers[bindingInfo.index].second);
				}

				// the arena keeps the table alive until the operation is completed
				auto table = scratch.allocate(privateDevice, addresses.data(), addresses.size() * sizeof(uint64_t), keepAliveBuffers);
				auto privateAddrBuf = static_cast<PrivateBuffer*>(table.first.get());

				auto& info = bufInfos.emplace_front();
				info.buffer = privateAddrBuf->buffer();
				info.offset = table.second;
				info.range = addresses.size() * sizeof(uint64_t);

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

				if (bindingInfo.type == BindingType::Texture) {
					std::shared_ptr<PrivateTexture> privateTexture;
					if (bindingInfo.index < functionResources.textures.size()) {
						privateTexture = std::dynamic_pointer_cast<PrivateTexture>(functionResources.textures[bindingInfo.index]);
					}
					// without nullDescriptor, an unbound texture can only be left unwritten
					if (!privateTexture && !privateDevice->nullDescriptorEnabled()) {
						continue;
					}

					auto& info = imageInfos.emplace_front();
					info.imageView = privateTexture ? privateTexture->sampledImageView() : VK_NULL_HANDLE;
					info.imageLayout = privateTexture ? privateTexture->imageLayout() : VK_IMAGE_LAYOUT_GENERAL;

					auto& descSet = writeDescSet.emplace_back();
					descSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					descSet.dstSet = descriptorSets[i];
					descSet.dstBinding = bindingInfo.internalIndex;
					descSet.dstArrayElement = 0;
					descSet.descriptorType = (bindingInfo.textureAccessType == TextureAccessType::Sample) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					descSet.descriptorCount = 1;
					descSet.pImageInfo = &info;
				} else if (bindingInfo.type == BindingType::Sampler) {
					bool embeddedSampler = false;

					if (bindingInfo.index == SIZE_MAX) {
						// this binding uses an embedded sampler
						embeddedSampler = true;
					}

					std::shared_ptr<SamplerState> sampler;
					if (embeddedSampler) {
						sampler = funcInfo.embeddedSamplerStates[bindingInfo.embeddedSamplerIndex];
					} else if (bindingInfo.index < functionResources.samplers.size()) {
						sampler = functionResources.samplers[bindingInfo.index];
					}
					if (!sampler) {
						sampler = privateDevice->fallbackSampler();
					}
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

};
