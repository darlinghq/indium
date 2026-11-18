// Copyright (C) 2026 Lubos Dolezel

#include <indium/heap.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/device.private.hpp>
#include <indium/memory-type.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

#include <unistd.h>

namespace {
	VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
		return (value + alignment - 1) / alignment * alignment;
	}

	// The memory types a buffer can be bound to.
	uint32_t bufferMemoryTypeBits(Indium::PrivateDevice& device) {
		auto info = Indium::bufferCreateInfo(256);
		VkBuffer buffer = VK_NULL_HANDLE;
		if (Indium::DynamicVK::vkCreateBuffer(device.device(), &info, nullptr, &buffer) != VK_SUCCESS) {
			return 0;
		}
		VkMemoryRequirements requirements {};
		Indium::DynamicVK::vkGetBufferMemoryRequirements(device.device(), buffer, &requirements);
		Indium::DynamicVK::vkDestroyBuffer(device.device(), buffer, nullptr);
		return requirements.memoryTypeBits;
	}

	// The memory types an optimally tiled color image with the usages most textures have can be bound to
	// (all of them if the image cannot be created, which leaves the choice to buffers).
	uint32_t imageMemoryTypeBits(Indium::PrivateDevice& device) {
		VkImageCreateInfo info {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = VK_FORMAT_R8G8B8A8_UNORM;
		info.extent = { 64, 64, 1 };
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			| VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImage image = VK_NULL_HANDLE;
		if (Indium::DynamicVK::vkCreateImage(device.device(), &info, nullptr, &image) != VK_SUCCESS) {
			return std::numeric_limits<uint32_t>::max();
		}
		VkMemoryRequirements requirements {};
		Indium::DynamicVK::vkGetImageMemoryRequirements(device.device(), image, &requirements);
		Indium::DynamicVK::vkDestroyImage(device.device(), image, nullptr);
		return requirements.memoryTypeBits;
	}
}

//
// HeapAllocation
//

Indium::HeapAllocation::HeapAllocation(std::shared_ptr<PrivateHeap> heap, uint64_t offset, uint64_t size, bool tracked):
	_heap(std::move(heap)),
	_offset(offset),
	_size(size),
	_tracked(tracked)
{};

Indium::HeapAllocation::~HeapAllocation() {
	makeAliasable();
};

void Indium::HeapAllocation::makeAliasable() {
	if (_tracked && !_released.exchange(true)) {
		_heap->give(_offset, _size);
	}
};

bool Indium::HeapAllocation::isAliasable() const {
	return !_tracked || _released.load();
};

//
// PrivateHeap
//

Indium::Heap::~Heap() {};

Indium::PrivateHeap::PrivateHeap(std::shared_ptr<PrivateDevice> device, const HeapDescriptor& descriptor):
	_device(device),
	_type(descriptor.type),
	_storageMode(descriptor.storageMode),
	_cpuCacheMode(descriptor.cpuCacheMode),
	_hazardTrackingMode(descriptor.hazardTrackingMode == HazardTrackingMode::HazardTrackingModeDefault ? HazardTrackingMode::HazardTrackingModeUntracked : descriptor.hazardTrackingMode)
{
	if (_type == HeapType::Sparse || (_storageMode != StorageMode::Shared && _storageMode != StorageMode::Private) || descriptor.size == 0) {
		throw std::runtime_error("This heap cannot be made");
	}

	const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
	if (descriptor.size > std::numeric_limits<size_t>::max() - page) {
		throw std::runtime_error("The heap is too large");
	}
	_size = alignUp(descriptor.size, page);

	// One memory object holds buffers and textures, so its type has to be one both can be bound to. A texture
	// the type does not suit is refused when it is made (HeapAllocation); the driver may allow fewer types for
	// images than for buffers, in which case the buffers decide.
	const auto& memory = _device->memoryProperties();
	uint32_t bufferBits = bufferMemoryTypeBits(*_device);
	size_t index = chooseMemoryType(memory, bufferBits & imageMemoryTypeBits(*_device), _storageMode);
	if (index == SIZE_MAX) {
		index = chooseMemoryType(memory, bufferBits, _storageMode);
	}
	if (index == SIZE_MAX) {
		throw std::runtime_error("No suitable memory region found for a heap with this storage mode");
	}
	_memoryTypeIndex = static_cast<uint32_t>(index);

	// Vulkan allows no allocation larger than maxMemoryAllocationSize, and one that is larger than the memory
	// heap it comes from cannot be backed; some drivers (lavapipe) would still accept it.
	VkPhysicalDeviceVulkan11Properties vk11Properties {};
	vk11Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
	VkPhysicalDeviceProperties2 properties2 {};
	properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties2.pNext = &vk11Properties;
	DynamicVK::vkGetPhysicalDeviceProperties2(_device->physicalDevice(), &properties2);
	if (_size > vk11Properties.maxMemoryAllocationSize || _size > memory.memoryHeaps[memory.memoryTypes[index].heapIndex].size) {
		throw std::runtime_error("The heap is larger than the device can allocate");
	}

	// buffers need the device address flag, so the memory is allocated as buffer memory
	_allocation = _device->memoryPool().allocateDedicated(_device->device(), _memoryTypeIndex, _size, MemoryPool::Kind::Buffer);
	_device->noteHeapCreated(_size);

	if (_type == HeapType::Automatic) {
		_ranges = RangeAllocator(_size);
	}
};

Indium::PrivateHeap::~PrivateHeap() {
	if (_allocation.memory != VK_NULL_HANDLE) {
		_device->noteHeapDestroyed(_size);
	}
	_device->memoryPool().free(_device->device(), _allocation);
};

std::shared_ptr<Indium::Device> Indium::PrivateHeap::device() {
	return _device;
};

size_t Indium::PrivateHeap::usedSize() const {
	std::scoped_lock lock(_mutex);
	return _used;
};

size_t Indium::PrivateHeap::maxAvailableSize(size_t alignment) const {
	if (_type != HeapType::Automatic) {
		return 0;
	}
	std::scoped_lock lock(_mutex);
	return _ranges.largestAvailable(alignment);
};

Indium::SizeAndAlign Indium::PrivateHeap::sizeAndAlign(PrivateDevice& device, StorageMode storageMode, const VkMemoryRequirements& requirements) {
	const auto& limits = device.properties().limits;
	VkDeviceSize unit = std::max<VkDeviceSize>(limits.bufferImageGranularity, 1);
	if (storageMode == StorageMode::Shared || storageMode == StorageMode::Managed) {
		unit = std::max<VkDeviceSize>(unit, limits.nonCoherentAtomSize);
	}
	SizeAndAlign result;
	result.size = alignUp(requirements.size, unit);
	result.align = std::max<VkDeviceSize>(requirements.alignment, unit);
	return result;
};

std::unique_ptr<Indium::HeapAllocation> Indium::PrivateHeap::reserve(const VkMemoryRequirements& requirements, std::optional<size_t> placementOffset) {
	if ((requirements.memoryTypeBits & (1u << _memoryTypeIndex)) == 0) {
		return nullptr;
	}

	auto sa = sizeAndAlign(*_device, _storageMode, requirements);

	if (_type == HeapType::Placement) {
		if (!placementOffset) {
			return nullptr;
		}
		uint64_t offset = *placementOffset;
		if (offset % sa.align != 0 || offset > _size || sa.size > _size - offset) {
			return nullptr;
		}
		return std::make_unique<HeapAllocation>(shared_from_this(), offset, sa.size, false);
	}

	if (placementOffset) {
		return nullptr;
	}

	std::scoped_lock lock(_mutex);
	auto offset = _ranges.allocate(sa.size, sa.align);
	if (!offset) {
		return nullptr;
	}
	_used += sa.size;
	return std::make_unique<HeapAllocation>(shared_from_this(), *offset, sa.size, true);
};

void Indium::PrivateHeap::give(uint64_t offset, uint64_t size) {
	std::scoped_lock lock(_mutex);
	_ranges.free(offset, size);
	_used -= size;
};

Indium::MemoryPool::Allocation Indium::PrivateHeap::subAllocation(const HeapAllocation& allocation) const {
	MemoryPool::Allocation result = _allocation;
	result.offset = _allocation.offset + allocation.offset();
	result.size = allocation.size();
	return result;
};

bool Indium::PrivateHeap::checkOptions(ResourceOptions options) const {
	auto storageMode = static_cast<StorageMode>((static_cast<size_t>(options) >> 4) & 0xf);
	auto cacheMode = static_cast<CPUCacheMode>(static_cast<size_t>(options) & 0xf);
	return storageMode == _storageMode && cacheMode == _cpuCacheMode;
};

std::shared_ptr<Indium::Buffer> Indium::PrivateHeap::newBuffer(size_t length, ResourceOptions options) {
	if (_type != HeapType::Automatic || length == 0 || !checkOptions(options)) {
		return nullptr;
	}
	try {
		return std::make_shared<PrivateBuffer>(_device, length, options, PrivateBuffer::HeapPlacement { shared_from_this(), std::nullopt });
	} catch (const std::runtime_error&) {
		return nullptr;
	}
};

std::shared_ptr<Indium::Buffer> Indium::PrivateHeap::newBuffer(size_t length, ResourceOptions options, size_t offset) {
	if (_type != HeapType::Placement || !checkOptions(options)) {
		return nullptr;
	}
	try {
		return std::make_shared<PrivateBuffer>(_device, length, options, PrivateBuffer::HeapPlacement { shared_from_this(), offset });
	} catch (const std::runtime_error&) {
		return nullptr;
	}
};

namespace {
	// A texture takes the memory of its heap whatever its storage mode asks for, except that it has to agree
	// with the heap unless it is memoryless.
	bool textureOptionsMatch(const Indium::TextureDescriptor& descriptor, Indium::StorageMode heapStorage, Indium::CPUCacheMode heapCache) {
		auto storageMode = static_cast<Indium::StorageMode>((static_cast<size_t>(descriptor.resourceOptions) >> 4) & 0xf);
		auto cacheMode = static_cast<Indium::CPUCacheMode>(static_cast<size_t>(descriptor.resourceOptions) & 0xf);
		return (storageMode == heapStorage || storageMode == Indium::StorageMode::Memoryless) && cacheMode == heapCache;
	}
}

std::shared_ptr<Indium::Texture> Indium::PrivateHeap::newTexture(const TextureDescriptor& descriptor) {
	if (_type != HeapType::Automatic || !textureOptionsMatch(descriptor, _storageMode, _cpuCacheMode)) {
		return nullptr;
	}
	try {
		ConcreteTexture::HeapBacking backing { shared_from_this(), std::nullopt, nullptr };
		return std::make_shared<ConcreteTexture>(_device, descriptor, nullptr, nullptr, &backing);
	} catch (const std::exception&) {
		return nullptr;
	}
};

std::shared_ptr<Indium::Texture> Indium::PrivateHeap::newTexture(const TextureDescriptor& descriptor, size_t offset) {
	if (_type != HeapType::Placement || !textureOptionsMatch(descriptor, _storageMode, _cpuCacheMode)) {
		return nullptr;
	}
	try {
		ConcreteTexture::HeapBacking backing { shared_from_this(), offset, nullptr };
		return std::make_shared<ConcreteTexture>(_device, descriptor, nullptr, nullptr, &backing);
	} catch (const std::exception&) {
		return nullptr;
	}
};

//
// PrivateDevice
//

std::shared_ptr<Indium::Heap> Indium::PrivateDevice::newHeap(const HeapDescriptor& descriptor) {
	ensureLogicalDevice();
	try {
		return std::make_shared<PrivateHeap>(shared_from_this(), descriptor);
	} catch (const std::runtime_error&) {
		return nullptr;
	}
};

Indium::SizeAndAlign Indium::PrivateDevice::heapBufferSizeAndAlign(size_t length, ResourceOptions options) {
	ensureLogicalDevice();
	auto storageMode = static_cast<StorageMode>((static_cast<size_t>(options) >> 4) & 0xf);

	auto info = bufferCreateInfo(std::max<size_t>(length, 1));
	VkBuffer buffer = VK_NULL_HANDLE;
	if (DynamicVK::vkCreateBuffer(_device, &info, nullptr, &buffer) != VK_SUCCESS) {
		return {};
	}
	VkMemoryRequirements requirements {};
	DynamicVK::vkGetBufferMemoryRequirements(_device, buffer, &requirements);
	DynamicVK::vkDestroyBuffer(_device, buffer, nullptr);
	if (length == 0) {
		requirements.size = 0;
	}
	return PrivateHeap::sizeAndAlign(*this, storageMode, requirements);
};

Indium::SizeAndAlign Indium::PrivateDevice::heapTextureSizeAndAlign(const TextureDescriptor& descriptor) {
	ensureLogicalDevice();
	auto storageMode = static_cast<StorageMode>((static_cast<size_t>(descriptor.resourceOptions) >> 4) & 0xf);

	VkMemoryRequirements requirements {};
	ConcreteTexture::HeapBacking query;
	query.query = &requirements;
	try {
		ConcreteTexture probe(shared_from_this(), descriptor, nullptr, nullptr, &query);
	} catch (const std::exception&) {
		return {};
	}
	return PrivateHeap::sizeAndAlign(*this, storageMode, requirements);
};
