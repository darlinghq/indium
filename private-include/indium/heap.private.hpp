#pragma once

// Copyright (C) 2026 Lubos Dolezel

#include <indium/heap.hpp>
#include <indium/memory-pool.private.hpp>
#include <indium/range-allocator.private.hpp>

#include <vulkan/vulkan.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace Indium {
	class PrivateDevice;
	class PrivateHeap;

	/**
	 * The part of a heap one resource takes. Owned by the resource: when it is destroyed (or `makeAliasable()` is
	 * called, whichever comes first) an automatic heap gets the range back. A placement heap does no bookkeeping, so
	 * there nothing is given back and the allocation is always aliasable.
	 */
	class HeapAllocation {
	public:
		HeapAllocation(std::shared_ptr<PrivateHeap> heap, uint64_t offset, uint64_t size, bool tracked);
		~HeapAllocation();
		HeapAllocation(const HeapAllocation&) = delete;
		HeapAllocation& operator=(const HeapAllocation&) = delete;

		const std::shared_ptr<PrivateHeap>& heap() const { return _heap; }
		/** The offset in the heap. */
		uint64_t offset() const { return _offset; }
		/** The size the resource counts for in the heap (see `Device::heapBufferSizeAndAlign()`). */
		uint64_t size() const { return _size; }

		void makeAliasable();
		bool isAliasable() const;

	private:
		std::shared_ptr<PrivateHeap> _heap;
		uint64_t _offset;
		uint64_t _size;
		bool _tracked;
		std::atomic<bool> _released { false };
	};

	class PrivateHeap: public Heap, public std::enable_shared_from_this<PrivateHeap> {
	public:
		/** Allocates the heap's memory; throws std::runtime_error if it cannot. */
		PrivateHeap(std::shared_ptr<PrivateDevice> device, const HeapDescriptor& descriptor);
		~PrivateHeap();

		virtual std::shared_ptr<Device> device() override;
		virtual HeapType type() const override { return _type; }
		virtual StorageMode storageMode() const override { return _storageMode; }
		virtual CPUCacheMode cpuCacheMode() const override { return _cpuCacheMode; }
		virtual HazardTrackingMode hazardTrackingMode() const override { return _hazardTrackingMode; }
		virtual size_t size() const override { return _size; }
		virtual size_t usedSize() const override;
		virtual size_t currentAllocatedSize() const override { return _size; }
		virtual size_t maxAvailableSize(size_t alignment) const override;

		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options) override;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor) override;
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options, size_t offset) override;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor, size_t offset) override;

		/**
		 * The size and alignment of a resource with `requirements` in a heap of `storageMode`, as the device rounds
		 * them: sizes and offsets are multiples of bufferImageGranularity (a buffer or linear image may not share
		 * one of those pages with an optimal image unless the two alias) and, in host-visible memory, of
		 * nonCoherentAtomSize (so that flushing one buffer never covers another's bytes).
		 */
		static SizeAndAlign sizeAndAlign(PrivateDevice& device, StorageMode storageMode, const VkMemoryRequirements& requirements);

		/**
		 * Takes room for a resource with `requirements` (an automatic heap picks the offset, a placement heap
		 * checks `placementOffset`). `nullptr` if the heap's memory type is not allowed for the resource, there is no
		 * room, or the offset is not aligned or leaves the heap.
		 */
		std::unique_ptr<HeapAllocation> reserve(const VkMemoryRequirements& requirements, std::optional<size_t> placementOffset);

		/** The device memory object and the memory type of the heap, and the offset in the memory of a heap offset. */
		VkDeviceMemory memory() const { return _allocation.memory; }
		uint32_t memoryTypeIndex() const { return _memoryTypeIndex; }
		VkDeviceSize memoryOffset(uint64_t heapOffset) const { return _allocation.offset + heapOffset; }

		/** The range of the heap's memory an allocation covers, as a pool allocation (to map and flush buffers). */
		MemoryPool::Allocation subAllocation(const HeapAllocation& allocation) const;

		void give(uint64_t offset, uint64_t size);

	private:
		std::shared_ptr<PrivateDevice> _device;
		HeapType _type;
		StorageMode _storageMode;
		CPUCacheMode _cpuCacheMode;
		HazardTrackingMode _hazardTrackingMode;
		size_t _size = 0;
		uint32_t _memoryTypeIndex = 0;
		MemoryPool::Allocation _allocation;

		mutable std::mutex _mutex;
		RangeAllocator _ranges { 0 };
		uint64_t _used = 0;

		bool checkOptions(ResourceOptions options) const;
	};
};
