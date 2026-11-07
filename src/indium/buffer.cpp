#include <indium/buffer.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstring>

// TODO: use the VulkanMemoryAllocator library to manage memory allocation efficiently

Indium::Buffer::~Buffer() {};

Indium::PrivateBuffer::PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, ResourceOptions options):
	_privateDevice(device),
	_length(length)
{
	_storageMode = static_cast<StorageMode>((static_cast<size_t>(options) >> 4) & 0xf);

	VkBufferCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = length;
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

	if (DynamicVK::vkCreateBuffer(_privateDevice->device(), &info, nullptr, &_buffer) != VK_SUCCESS) {
		// TODO
		abort();
	}

	VkMemoryRequirements requirements;

	DynamicVK::vkGetBufferMemoryRequirements(_privateDevice->device(), _buffer, &requirements);

	size_t targetIndex = _privateDevice->findMemoryType(requirements.memoryTypeBits, _storageMode);

	if (targetIndex == SIZE_MAX) {
		throw std::runtime_error("No suitable memory region found for buffer with requested storage mode");
	}

	try {
		_allocation = _privateDevice->memoryPool().allocate(_privateDevice->device(), targetIndex, requirements, MemoryPool::Kind::Buffer);
	} catch (...) {
		DynamicVK::vkDestroyBuffer(_privateDevice->device(), _buffer, nullptr);
		throw;
	}
	_memory = _allocation.memory;

	DynamicVK::vkBindBufferMemory(_privateDevice->device(), _buffer, _memory, _allocation.offset);
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
	_privateDevice->memoryPool().free(_privateDevice->device(), _allocation);
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
