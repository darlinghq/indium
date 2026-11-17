#include <indium/buffer.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

// TODO: use the VulkanMemoryAllocator library to manage memory allocation efficiently

Indium::Buffer::~Buffer() {};

VkBufferCreateInfo Indium::bufferCreateInfo(VkDeviceSize size) {
	VkBufferCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size;
	info.usage =
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		;

	// TODO: maybe make this CONCURRENT instead? we already know all the queue families that can access it;
	//       that info is available in the PrivateDevice instance.
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	return info;
};

Indium::PrivateBuffer::PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, ResourceOptions options):
	_privateDevice(device),
	_length(length)
{
	_storageMode = static_cast<StorageMode>((static_cast<size_t>(options) >> 4) & 0xf);
	_hazardTrackingMode = resolveHazardTrackingMode(options, HazardTrackingMode::HazardTrackingModeDefault);

	auto info = bufferCreateInfo(length);

	if (DynamicVK::vkCreateBuffer(_privateDevice->device(), &info, nullptr, &_buffer) != VK_SUCCESS) {
		// TODO
		abort();
	}

	VkMemoryRequirements requirements;

	DynamicVK::vkGetBufferMemoryRequirements(_privateDevice->device(), _buffer, &requirements);
	_requiredSize = requirements.size;

	size_t targetIndex = _privateDevice->findMemoryType(requirements.memoryTypeBits, _storageMode);

	if (targetIndex == SIZE_MAX) {
		throw std::runtime_error("No suitable memory region found for buffer with requested storage mode");
	}

	_memoryTypeIndex = static_cast<uint32_t>(targetIndex);

	try {
		_allocation = _privateDevice->memoryPool().allocate(_privateDevice->device(), targetIndex, requirements, MemoryPool::Kind::Buffer);
	} catch (...) {
		DynamicVK::vkDestroyBuffer(_privateDevice->device(), _buffer, nullptr);
		throw;
	}
	_memory = _allocation.memory;

	DynamicVK::vkBindBufferMemory(_privateDevice->device(), _buffer, _memory, _allocation.offset);
};

Indium::PrivateBuffer::PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, ResourceOptions options, const HeapPlacement& placement):
	_privateDevice(device),
	_length(length)
{
	_storageMode = static_cast<StorageMode>((static_cast<size_t>(options) >> 4) & 0xf);
	_hazardTrackingMode = resolveHazardTrackingMode(options, placement.heap->hazardTrackingMode());

	// a Vulkan buffer has at least one byte
	auto info = bufferCreateInfo(std::max<size_t>(length, 1));

	if (DynamicVK::vkCreateBuffer(_privateDevice->device(), &info, nullptr, &_buffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a buffer");
	}

	VkMemoryRequirements requirements;
	DynamicVK::vkGetBufferMemoryRequirements(_privateDevice->device(), _buffer, &requirements);
	// the length is what a heap counts, so that a zero-length buffer takes nothing
	requirements.size = length == 0 ? 0 : requirements.size;

	_heapAllocation = placement.heap->reserve(requirements, placement.offset);
	if (!_heapAllocation) {
		DynamicVK::vkDestroyBuffer(_privateDevice->device(), _buffer, nullptr);
		throw std::runtime_error("The heap cannot place the buffer");
	}

	_requiredSize = _heapAllocation->size();
	_memoryTypeIndex = placement.heap->memoryTypeIndex();
	_allocation = placement.heap->subAllocation(*_heapAllocation);
	_memory = _allocation.memory;

	DynamicVK::vkBindBufferMemory(_privateDevice->device(), _buffer, _memory, placement.heap->memoryOffset(_heapAllocation->offset()));
};

Indium::PrivateBuffer::PrivateBuffer(std::shared_ptr<PrivateDevice> device, const void* pointer, size_t length, ResourceOptions options):
	PrivateBuffer(device, length, options)
{
	auto ptr = contents();

	if (!ptr) {
		// TODO: support non-host-visible memory
		abort();
	}

	memcpy(ptr, pointer, length);

	if (_storageMode == StorageMode::Managed) {
		// the memory may hold other resources too, so flush only this buffer's part of it
		auto range = MemoryPool::flushRange(_allocation, 0, length, _privateDevice->properties().limits.nonCoherentAtomSize);
		if (range.size != 0 && DynamicVK::vkFlushMappedMemoryRanges(_privateDevice->device(), 1, &range) != VK_SUCCESS) {
			// TODO
			abort();
		}
	}
};

Indium::PrivateBuffer::~PrivateBuffer() {
	// the mapping belongs to the memory block and goes away with it
	DynamicVK::vkDestroyBuffer(_privateDevice->device(), _buffer, nullptr);
	if (!_heapAllocation) {
		_privateDevice->memoryPool().free(_privateDevice->device(), _allocation);
	}
	// a heap buffer's range goes back to its heap with _heapAllocation
};

std::shared_ptr<Indium::Device> Indium::PrivateBuffer::device() {
	return _privateDevice;
};

size_t Indium::PrivateBuffer::length() const {
	return _length;
};

void* Indium::PrivateBuffer::contents() {
	if (_storageMode != StorageMode::Managed && _storageMode != StorageMode::Shared) {
		return nullptr;
	}

	if (!_mapped) {
		_mapped = _privateDevice->memoryPool().map(_privateDevice->device(), _allocation);
		if (!_mapped) {
			// TODO
			abort();
		}
	}

	return _mapped;
};

void Indium::PrivateBuffer::didModifyRange(Range<size_t> range) {
	// Vulkan only flushes whole multiples of nonCoherentAtomSize (or up to the end of the memory)
	auto vulkanRange = MemoryPool::flushRange(_allocation, range.start, range.length, _privateDevice->properties().limits.nonCoherentAtomSize);
	if (vulkanRange.size == 0) {
		return;
	}
	if (DynamicVK::vkFlushMappedMemoryRanges(_privateDevice->device(), 1, &vulkanRange) != VK_SUCCESS) {
		// TODO
		abort();
	}
};

uint64_t Indium::PrivateBuffer::gpuAddress() {
	// a buffer's device address never changes, and every draw asks for it once per bound buffer
	if (_gpuAddress) {
		return _gpuAddress;
	}
	VkBufferDeviceAddressInfo info {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	info.buffer = _buffer;
	_gpuAddress = DynamicVK::vkGetBufferDeviceAddress(_privateDevice->device(), &info);
	return _gpuAddress;
};

std::shared_ptr<Indium::Texture> Indium::PrivateBuffer::newTexture(const TextureDescriptor& descriptor, size_t offset, size_t bytesPerRow) {
	// the texture shares the buffer's memory, so it has the buffer's storage mode
	auto textureStorageMode = static_cast<StorageMode>((static_cast<size_t>(descriptor.resourceOptions) >> 4) & 0xf);
	// Metal asks for rows and a start that are a multiple of 16 bytes
	if (textureStorageMode != _storageMode || bytesPerRow == 0 || bytesPerRow % 16 != 0 || offset % 16 != 0) {
		return nullptr;
	}

	// First the texture whose image lies in the buffer's memory, which needs no copies, at the row pitch the
	// device gives a linear image (or, with a DRM format modifier, at the pitch asked for); then one that has an
	// image of its own and is copied to and from the buffer around the encoders that use it.
	for (bool shadow: { false, true }) {
		ConcreteTexture::BufferBacking backing { shared_from_this(), offset, bytesPerRow, shadow };
		try {
			return std::make_shared<ConcreteTexture>(_privateDevice, descriptor, nullptr, &backing);
		} catch (const std::runtime_error&) {
			// a format, usage or row pitch this kind of texture cannot have
		}
	}
	return nullptr;
};

std::shared_ptr<Indium::Heap> Indium::PrivateBuffer::heap() {
	return _heapAllocation ? _heapAllocation->heap() : nullptr;
};

size_t Indium::PrivateBuffer::heapOffset() {
	// only a placement heap's resources have an offset the caller chose
	if (_heapAllocation && _heapAllocation->heap()->type() == HeapType::Placement) {
		return _heapAllocation->offset();
	}
	return 0;
};

size_t Indium::PrivateBuffer::allocatedSize() {
	return _requiredSize;
};

void Indium::PrivateBuffer::makeAliasable() {
	if (_heapAllocation) {
		_heapAllocation->makeAliasable();
	}
};

bool Indium::PrivateBuffer::isAliasable() {
	return _heapAllocation && _heapAllocation->isAliasable();
};
