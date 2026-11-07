// Copyright (C) 2026 Darling Developers

#include <indium/memory-pool.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <iterator>
#include <stdexcept>

struct Indium::MemoryPool::Block {
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
	uint32_t key = 0;
	bool dedicated = false;
	void* mapped = nullptr;
	size_t live = 0;
	// offset -> size of each free range, coalesced
	std::map<VkDeviceSize, VkDeviceSize> free;
};

namespace {
	VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
		return (value + alignment - 1) / alignment * alignment;
	}

	VkDeviceMemory allocateMemory(VkDevice device, uint32_t typeIndex, VkDeviceSize size, bool deviceAddress) {
		VkMemoryAllocateFlagsInfo flags {};
		flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

		VkMemoryAllocateInfo info {};
		info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		info.allocationSize = size;
		info.memoryTypeIndex = typeIndex;
		info.pNext = deviceAddress ? &flags : nullptr;

		VkDeviceMemory memory = VK_NULL_HANDLE;
		if (Indium::DynamicVK::vkAllocateMemory(device, &info, nullptr, &memory) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate device memory");
		}
		return memory;
	}

	bool isEmpty(const Indium::MemoryPool::Block& block) {
		return block.live == 0 && block.free.size() == 1 && block.free.begin()->second == block.size;
	}
};

Indium::MemoryPool::Allocation Indium::MemoryPool::allocate(VkDevice device, uint32_t typeIndex, const VkMemoryRequirements& requirements, Kind kind) {
	const bool deviceAddress = kind == Kind::Buffer;
	Allocation result;

	if (requirements.size > maxPooledSize) {
		auto block = std::make_shared<Block>();
		block->dedicated = true;
		block->size = requirements.size;
		block->memory = allocateMemory(device, typeIndex, requirements.size, deviceAddress);
		result.memory = block->memory;
		result.offset = 0;
		result.size = requirements.size;
		result.block = std::move(block);
		return result;
	}

	const VkDeviceSize size = alignUp(requirements.size, granule);
	const VkDeviceSize alignment = std::max<VkDeviceSize>(requirements.alignment, granule);
	const uint32_t key = (typeIndex << 2) | static_cast<uint32_t>(kind);

	std::scoped_lock lock(_mutex);
	auto& list = _blocks[key];

	auto place = [&](const std::shared_ptr<Block>& block) {
		for (auto it = block->free.begin(); it != block->free.end(); ++it) {
			VkDeviceSize rangeStart = it->first;
			VkDeviceSize rangeEnd = it->first + it->second;
			VkDeviceSize start = alignUp(rangeStart, alignment);
			if (start + size > rangeEnd) {
				continue;
			}
			block->free.erase(it);
			if (start > rangeStart) {
				block->free[rangeStart] = start - rangeStart;
			}
			if (start + size < rangeEnd) {
				block->free[start + size] = rangeEnd - (start + size);
			}
			++block->live;
			result.memory = block->memory;
			result.offset = start;
			result.size = size;
			result.block = block;
			return true;
		}
		return false;
	};

	for (auto& block: list) {
		if (place(block)) {
			return result;
		}
	}

	auto block = std::make_shared<Block>();
	block->key = key;
	block->size = blockSize;
	block->memory = allocateMemory(device, typeIndex, blockSize, deviceAddress);
	block->free[0] = blockSize;
	list.push_back(block);
	place(block);
	return result;
};

void Indium::MemoryPool::free(VkDevice device, Allocation& allocation) {
	auto block = std::move(allocation.block);
	VkDeviceSize offset = allocation.offset;
	VkDeviceSize size = allocation.size;
	allocation = Allocation {};
	if (!block) {
		return;
	}

	if (block->dedicated) {
		DynamicVK::vkFreeMemory(device, block->memory, nullptr);
		return;
	}

	std::scoped_lock lock(_mutex);

	// merge with the free neighbours
	auto next = block->free.lower_bound(offset);
	if (next != block->free.begin()) {
		auto previous = std::prev(next);
		if (previous->first + previous->second == offset) {
			offset = previous->first;
			size += previous->second;
			block->free.erase(previous);
		}
	}
	if (next != block->free.end() && offset + size == next->first) {
		size += next->second;
		block->free.erase(next);
	}
	block->free[offset] = size;
	--block->live;

	if (!isEmpty(*block)) {
		return;
	}

	// keep one empty block of each kind for the next allocation, give the others back
	auto& list = _blocks[block->key];
	size_t empty = std::count_if(list.begin(), list.end(), [](const std::shared_ptr<Block>& other) { return isEmpty(*other); });
	if (empty <= 1) {
		return;
	}
	list.erase(std::find(list.begin(), list.end(), block));
	DynamicVK::vkFreeMemory(device, block->memory, nullptr);
	block->memory = VK_NULL_HANDLE;
};

void* Indium::MemoryPool::map(VkDevice device, const Allocation& allocation) {
	auto& block = allocation.block;
	if (!block) {
		return nullptr;
	}

	std::scoped_lock lock(_mutex);
	if (!block->mapped) {
		if (DynamicVK::vkMapMemory(device, block->memory, 0, VK_WHOLE_SIZE, 0, &block->mapped) != VK_SUCCESS) {
			block->mapped = nullptr;
			return nullptr;
		}
	}
	return static_cast<char*>(block->mapped) + allocation.offset;
};

void Indium::MemoryPool::releaseAll(VkDevice device) {
	std::scoped_lock lock(_mutex);
	for (auto& [key, list]: _blocks) {
		for (auto& block: list) {
			DynamicVK::vkFreeMemory(device, block->memory, nullptr);
			block->memory = VK_NULL_HANDLE;
		}
	}
	_blocks.clear();
};

VkMappedMemoryRange Indium::MemoryPool::flushRange(const Allocation& allocation, VkDeviceSize start, VkDeviceSize length, VkDeviceSize atomSize) {
	VkMappedMemoryRange range {};
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.memory = allocation.memory;

	if (atomSize == 0) {
		atomSize = 1;
	}
	VkDeviceSize first = start / atomSize * atomSize;
	VkDeviceSize last = std::min(alignUp(start + length, atomSize), allocation.size);
	if (length == 0 || first >= last) {
		return range;
	}

	// A pooled allocation starts on a granule and spans whole granules, which are multiples of
	// any valid atom size. A dedicated allocation starts at 0, and if `last` was clipped to its
	// size, the range ends at the end of the memory object, which Vulkan also allows.
	range.offset = allocation.offset + first;
	range.size = last - first;
	return range;
};
