#pragma once

#include <indium/buffer.hpp>

#include <indium/memory-pool.private.hpp>
#include <indium/heap.private.hpp>

#include <vulkan/vulkan.h>

#include <memory>
#include <optional>

namespace Indium {
	class PrivateDevice;

	/** The creation parameters of every buffer: all the usages Metal lets a buffer be used for. */
	VkBufferCreateInfo bufferCreateInfo(VkDeviceSize size);

	class PrivateBuffer: public Buffer, public std::enable_shared_from_this<PrivateBuffer> {
	private:
		std::shared_ptr<PrivateDevice> _privateDevice;
		size_t _length;
		StorageMode _storageMode;
		void* _mapped = nullptr;

	public:
		PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, ResourceOptions options);
		PrivateBuffer(std::shared_ptr<PrivateDevice> device, const void* pointer, size_t length, ResourceOptions options);

		// A buffer in the memory of a heap, at `offset` for a placement heap. Throws std::runtime_error if the
		// heap cannot place it.
		struct HeapPlacement {
			std::shared_ptr<PrivateHeap> heap;
			std::optional<size_t> offset;
		};
		PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, ResourceOptions options, const HeapPlacement& placement);
		~PrivateBuffer();

		virtual std::shared_ptr<Device> device() override;

		virtual size_t length() const override;
		virtual void* contents() override;
		virtual void didModifyRange(Range<size_t> range) override;

		virtual uint64_t gpuAddress() override;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor, size_t offset, size_t bytesPerRow) override;

		virtual std::shared_ptr<Heap> heap() override;
		virtual size_t heapOffset() override;
		virtual size_t allocatedSize() override;
		virtual void makeAliasable() override;
		virtual bool isAliasable() override;

		/** The memory the buffer is bound to: where it starts in `memory()`, how many bytes are reserved for it and the memory type. */
		VkDeviceSize memoryOffset() const { return _allocation.offset; }
		VkDeviceSize memorySize() const { return _allocation.size; }
		uint32_t memoryTypeIndex() const { return _memoryTypeIndex; }

		INDIUM_PROPERTY(VkBuffer, b, B,uffer) = VK_NULL_HANDLE;
		INDIUM_PROPERTY(VkDeviceMemory, m, M,emory) = VK_NULL_HANDLE;

	private:
		// after the members other libraries' inline accessors read
		uint64_t _gpuAddress = 0;
		MemoryPool::Allocation _allocation;
		uint32_t _memoryTypeIndex = 0;
		VkDeviceSize _requiredSize = 0;
		// set for a buffer made from a heap, which owns the memory
		std::unique_ptr<HeapAllocation> _heapAllocation;
	};
};
