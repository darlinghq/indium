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
	/**
	 * Whether `size` bytes of indirect arguments at `offset` can be read from `buffer`: a Vulkan indirect
	 * command may only read inside the buffer, from an offset that is a multiple of 4 (which is also the
	 * alignment Metal asks for). An indirect command that fails this is not recorded.
	 */
	inline bool indirectArgumentsFit(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t size) {
		return buffer && offset % 4 == 0 && offset <= buffer->length() && size <= buffer->length() - offset;
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
		// every texture that was ever set, including the ones a later call replaced (the draws and dispatches
		// in between used them)
		std::vector<std::shared_ptr<Texture>> usedTextures;
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
			if (texture && std::find(usedTextures.begin(), usedTextures.end(), texture) == usedTextures.end()) {
				usedTextures.push_back(texture);
			}
		};
	};

	/**
	 * Tells `fn(range, access)` about every tracked resource the function can reach through the bindings it was
	 * compiled with, with the access it may make: a texture as its binding says, a buffer as read and written
	 * (the binding does not say whether the function writes through it). Returns whether it can also reach a
	 * resource that is not tracked, which the caller orders itself, if the encoder says anything about it.
	 */
	template<typename Fn>
	bool forEachBoundResource(const FunctionResources& resources, const FunctionInfo& info, Fn&& fn) {
		bool untracked = false;
		for (const auto& binding: info.bindings) {
			if (binding.type == BindingType::Buffer) {
				if (binding.index >= resources.buffers.size()) {
					continue;
				}
				auto buffer = std::dynamic_pointer_cast<PrivateBuffer>(resources.buffers[binding.index].first);
				if (!buffer) {
					continue;
				}
				if (buffer->hazardTrackingMode() == HazardTrackingMode::HazardTrackingModeUntracked) {
					untracked = true;
					continue;
				}
				fn(buffer->hazardRange(resources.buffers[binding.index].second), VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
			} else if (binding.type == BindingType::Texture) {
				if (binding.index >= resources.textures.size()) {
					continue;
				}
				auto texture = std::dynamic_pointer_cast<PrivateTexture>(resources.textures[binding.index]);
				if (!texture) {
					continue;
				}
				if (texture->hazardTrackingMode() == HazardTrackingMode::HazardTrackingModeUntracked) {
					untracked = true;
					continue;
				}
				VkAccessFlags2 access = 0;
				switch (binding.textureAccessType) {
					case TextureAccessType::Sample:
					case TextureAccessType::Read:
						access = VK_ACCESS_2_SHADER_READ_BIT;
						break;
					case TextureAccessType::Write:
						access = VK_ACCESS_2_SHADER_WRITE_BIT;
						break;
					case TextureAccessType::ReadWrite:
						access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
						break;
				}
				fn(texture->hazardRange(), access);
			}
		}
		return untracked;
	}

	/**
	 * What a resource declared with useResource: or useHeap: is accessed with, for the stages that may reach it.
	 * Metal asks for the declaration because the resource is reached through memory the encoder cannot see into
	 * (an argument buffer, a pointer written into a buffer), so nothing but the declaration says it is used.
	 */
	inline VkAccessFlags2 resourceUsageAccess(ResourceUsage usage) {
		VkAccessFlags2 access = 0;
		if (!!(usage & (ResourceUsage::Read | ResourceUsage::Sample))) {
			access |= VK_ACCESS_2_SHADER_READ_BIT;
		}
		if (!!(usage & ResourceUsage::Write)) {
			access |= VK_ACCESS_2_SHADER_WRITE_BIT;
		}
		return access;
	}

	/**
	 * Adds the range of a resource that `useResource` names to `set`. A heap that is tracked is added whole. Returns false
	 * for a resource that is not tracked (or is neither a buffer, a texture nor a heap): its ordering is not
	 * Indium's to add.
	 */
	inline bool addDeclaredResource(HazardAccessSet& set, const std::shared_ptr<Resource>& resource, ResourceUsage usage, VkPipelineStageFlags2 stages) {
		if (!resource || resource->hazardTrackingMode() == HazardTrackingMode::HazardTrackingModeUntracked) {
			return false;
		}
		auto access = resourceUsageAccess(usage);
		if (auto buffer = std::dynamic_pointer_cast<PrivateBuffer>(resource)) {
			set.add(buffer->hazardRange(), stages, access);
			return true;
		}
		if (auto texture = std::dynamic_pointer_cast<PrivateTexture>(resource)) {
			set.add(texture->hazardRange(), stages, access);
			return true;
		}
		return false;
	}

	inline bool addDeclaredHeap(HazardAccessSet& set, const std::shared_ptr<Heap>& heap, VkPipelineStageFlags2 stages) {
		auto privateHeap = std::dynamic_pointer_cast<PrivateHeap>(heap);
		// a heap's resources take the heap's mode unless they say otherwise, and an untracked heap's are the application's to order
		if (!privateHeap || privateHeap->hazardTrackingMode() != HazardTrackingMode::HazardTrackingModeTracked) {
			return false;
		}
		set.add(HazardRange::memory(privateHeap->memory(), privateHeap->memoryOffset(0), privateHeap->memoryOffset(0) + privateHeap->size()), stages, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
		return true;
	}

	static constexpr std::array<VkDescriptorPoolSize, 6> poolSizes {
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLER, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 512 },
		VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 512 },
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
			std::forward_list<VkBufferView> bufferViews;

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

					// a texture_buffer is bound through the buffer view of its texture and any other texture through
					// its image view (a storage image may not be swizzled, so it never gets the view a sampler reads
					// through). A texture of the other kind has no such view, which is left unwritten like an unbound one.
					bool sampled = bindingInfo.textureAccessType == TextureAccessType::Sample;
					VkImageView imageView = VK_NULL_HANDLE;
					VkBufferView bufferView = VK_NULL_HANDLE;
					if (privateTexture) {
						if (bindingInfo.texelBuffer) {
							bufferView = privateTexture->bufferView();
						} else {
							imageView = sampled ? privateTexture->sampledImageView() : privateTexture->imageView();
						}
						if (bufferView == VK_NULL_HANDLE && imageView == VK_NULL_HANDLE && !privateDevice->nullDescriptorEnabled()) {
							continue;
						}
					}

					auto& descSet = writeDescSet.emplace_back();
					descSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					descSet.dstSet = descriptorSets[i];
					descSet.dstBinding = bindingInfo.internalIndex;
					descSet.dstArrayElement = 0;
					descSet.descriptorType = textureDescriptorType(bindingInfo);
					descSet.descriptorCount = 1;

					if (bindingInfo.texelBuffer) {
						descSet.pTexelBufferView = &bufferViews.emplace_front(bufferView);
					} else {
						auto& info = imageInfos.emplace_front();
						info.imageView = imageView;
						info.imageLayout = privateTexture ? privateTexture->imageLayout() : VK_IMAGE_LAYOUT_GENERAL;
						descSet.pImageInfo = &info;
					}
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
