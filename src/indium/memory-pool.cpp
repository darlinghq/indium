// Copyright (C) 2026 Darling Developers

#include <indium/memory-pool.private.hpp>
#include <indium/range-allocator.private.hpp>
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
	RangeAllocator ranges { 0 };
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
		return block.live == 0 && block.ranges.isEmpty();
	}
};

Indium::MemoryPool::Allocation Indium::MemoryPool::allocateDedicated(VkDevice device, uint32_t typeIndex, VkDeviceSize size, Kind kind) {
	auto block = std::make_shared<Block>();
	block->dedicated = true;
	block->size = size;
	block->memory = allocateMemory(device, typeIndex, size, kind == Kind::Buffer);

	Allocation result;
	result.memory = block->memory;
	result.offset = 0;
	result.size = size;
	result.block = std::move(block);
	++_liveMemoryObjects;
	++_totalMemoryObjects;
	noteAllocated(size);
	return result;
};

Indium::MemoryPool::Allocation Indium::MemoryPool::allocate(VkDevice device, uint32_t typeIndex, const VkMemoryRequirements& requirements, Kind kind) {
	if (requirements.size > maxPooledSize) {
		return allocateDedicated(device, typeIndex, requirements.size, kind);
	}

	const bool deviceAddress = kind == Kind::Buffer;
	Allocation result;

	const VkDeviceSize size = alignUp(requirements.size, granule);
	const VkDeviceSize alignment = std::max<VkDeviceSize>(requirements.alignment, granule);
	const uint32_t key = (typeIndex << 2) | static_cast<uint32_t>(kind);

	std::scoped_lock lock(_mutex);
	auto& list = _blocks[key];

	auto place = [&](const std::shared_ptr<Block>& block) {
		auto start = block->ranges.allocate(size, alignment);
		if (!start) {
			return false;
		}
		++block->live;
		result.memory = block->memory;
		result.offset = *start;
		result.size = size;
		result.block = block;
		return true;
	};

	for (auto& block: list) {
		if (place(block)) {
			noteAllocated(size);
			return result;
		}
	}

	auto block = std::make_shared<Block>();
	block->key = key;
	block->size = blockSize;
	block->memory = allocateMemory(device, typeIndex, blockSize, deviceAddress);
	block->ranges = RangeAllocator(blockSize);
	list.push_back(block);
	place(block);
	++_liveMemoryObjects;
	++_totalMemoryObjects;
	noteAllocated(size);
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

	noteFreed(size);

	if (block->dedicated) {
		DynamicVK::vkFreeMemory(device, block->memory, nullptr);
		--_liveMemoryObjects;
		return;
	}

	std::scoped_lock lock(_mutex);

	block->ranges.free(offset, size);
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
	--_liveMemoryObjects;
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

VkResult Indium::MemoryPool::bindBuffer(VkDevice device, VkBuffer buffer, const Allocation& allocation) {
	std::scoped_lock lock(_mutex);
	return DynamicVK::vkBindBufferMemory(device, buffer, allocation.memory, allocation.offset);
};

VkResult Indium::MemoryPool::bindImage(VkDevice device, VkImage image, const Allocation& allocation) {
	std::scoped_lock lock(_mutex);
	return DynamicVK::vkBindImageMemory(device, image, allocation.memory, allocation.offset);
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

void Indium::MemoryPool::noteAllocated(VkDeviceSize size) {
	uint64_t live = _liveBytes.fetch_add(size) + size;
	++_liveAllocations;
	++_totalAllocations;
	_totalBytes += size;
	uint64_t peak = _peakBytes.load();
	while (live > peak && !_peakBytes.compare_exchange_weak(peak, live)) {
	}
};

void Indium::MemoryPool::noteFreed(VkDeviceSize size) {
	_liveBytes -= size;
	--_liveAllocations;
};

Indium::MemoryPool::Counters Indium::MemoryPool::counters() const {
	return Counters {
		_liveBytes.load(), _peakBytes.load(), _liveAllocations.load(), _totalAllocations.load(),
		_totalBytes.load(), _liveMemoryObjects.load(), _totalMemoryObjects.load(),
	};
};
