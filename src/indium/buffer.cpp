#include <indium/buffer.private.hpp>
#include <indium/device.private.hpp>

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

	if (vkCreateBuffer(_privateDevice->device(), &info, nullptr, &_buffer) != VK_SUCCESS) {
		// TODO
		abort();
	}

	VkMemoryRequirements requirements;

	vkGetBufferMemoryRequirements(_privateDevice->device(), _buffer, &requirements);

	size_t targetIndex = 0;

	for (size_t i = 0; i < _privateDevice->memoryProperties().memoryTypeCount; ++i) {
		const auto& type = _privateDevice->memoryProperties().memoryTypes[i];

		if ((requirements.memoryTypeBits & (1 << i)) == 0) {
			continue;
		}

		if ((_storageMode == StorageMode::Managed || _storageMode == StorageMode::Shared) && (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
			continue;
		}

		if (_storageMode == StorageMode::Shared && (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
			continue;
		}

		// okay, this is good enough
		targetIndex = i;
		break;
	}

	VkMemoryAllocateInfo allocateInfo {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = targetIndex;

	VkMemoryAllocateFlagsInfo allocateFlags {};
	allocateFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	allocateFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	allocateInfo.pNext = &allocateFlags;

	if (vkAllocateMemory(_privateDevice->device(), &allocateInfo, nullptr, &_memory) != VK_SUCCESS) {
		// TODO
		abort();
	}

	vkBindBufferMemory(_privateDevice->device(), _buffer, _memory, 0);
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
		VkMappedMemoryRange range {};
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = _memory;
		range.size = VK_WHOLE_SIZE;
		range.offset = 0;
		if (vkFlushMappedMemoryRanges(_privateDevice->device(), 1, &range) != VK_SUCCESS) {
			// TODO
			abort();
		}
	}
};

Indium::PrivateBuffer::~PrivateBuffer() {
	if (_mapped) {
		vkUnmapMemory(_privateDevice->device(), _memory);
	}

	vkDestroyBuffer(_privateDevice->device(), _buffer, nullptr);
	vkFreeMemory(_privateDevice->device(), _memory, nullptr);
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
		if (vkMapMemory(_privateDevice->device(), _memory, 0, VK_WHOLE_SIZE, 0, &_mapped) != VK_SUCCESS) {
			// TODO
			abort();
		}
	}

	return _mapped;
};

void Indium::PrivateBuffer::didModifyRange(Range<size_t> range) {
	VkMappedMemoryRange vulkanRange {};
	vulkanRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	vulkanRange.memory = _memory;
	vulkanRange.size = range.length;
	vulkanRange.offset = range.start;
	if (vkFlushMappedMemoryRanges(_privateDevice->device(), 1, &vulkanRange) != VK_SUCCESS) {
		// TODO
		abort();
	}
};
