#pragma once

// Copyright (C) 2026 Darling Developers

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Indium {
	/**
	 * Suballocation of device memory for buffers and textures.
	 *
	 * Giving every resource a VkDeviceMemory of its own makes allocation a driver (and often
	 * kernel) call per resource, counts against maxMemoryAllocationCount, and on some drivers
	 * makes every queue submission cost more the more allocations are alive. Resources up to
	 * maxPooledSize are placed in shared blocks of blockSize bytes instead; larger ones keep an
	 * allocation of their own.
	 *
	 * Blocks are kept apart by memory type and by resource kind: buffers need memory allocated
	 * with VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, and linear and optimally tiled resources may
	 * not share bufferImageGranularity-sized pages, so optimal images never share a block with
	 * buffers or linear images.
	 *
	 * Resources are destroyed only once no command buffer uses them any more (encoders keep them
	 * alive until their command buffer completes), so a freed range can be reused at once, just
	 * as a freed dedicated allocation could.
	 */
	class MemoryPool {
	public:
		static constexpr VkDeviceSize blockSize = 32ull << 20;
		static constexpr VkDeviceSize maxPooledSize = 4ull << 20;
		// placement granularity: at least Vulkan's largest permitted nonCoherentAtomSize, so a
		// flush of whole granules never touches another resource's bytes
		static constexpr VkDeviceSize granule = 256;

		enum class Kind: uint32_t {
			Buffer = 0,
			LinearImage = 1,
			OptimalImage = 2,
		};

		struct Block;

		struct Allocation {
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkDeviceSize offset = 0;
			// the bytes reserved for the resource; flushes may cover all of them
			VkDeviceSize size = 0;
			std::shared_ptr<Block> block;
		};

		MemoryPool() = default;
		MemoryPool(const MemoryPool&) = delete;
		MemoryPool& operator=(const MemoryPool&) = delete;

		/**
		 * Memory for a resource with the given requirements. Throws std::runtime_error if the
		 * driver cannot provide it.
		 */
		Allocation allocate(VkDevice device, uint32_t typeIndex, const VkMemoryRequirements& requirements, Kind kind);

		/**
		 * A block of `size` bytes that is not shared with any other resource: the whole memory object is
		 * the allocation (offset 0), and freeing it frees the memory object. For things that sub-allocate
		 * themselves, such as a heap. `size` is not rounded.
		 */
		Allocation allocateDedicated(VkDevice device, uint32_t typeIndex, VkDeviceSize size, Kind kind);

		/**
		 * Gives the allocation's range back. The resource bound to it must be destroyed already.
		 */
		void free(VkDevice device, Allocation& allocation);

		/**
		 * A host pointer to the start of the allocation; the memory type must be host-visible.
		 * Blocks are mapped once and stay mapped.
		 */
		void* map(VkDevice device, const Allocation& allocation);

		/**
		 * Bind a buffer or image to its allocation. Mapping a block takes its memory object as an externally
		 * synchronized parameter, so binding another resource to the block's memory must not overlap with a map.
		 */
		VkResult bindBuffer(VkDevice device, VkBuffer buffer, const Allocation& allocation);
		VkResult bindImage(VkDevice device, VkImage image, const Allocation& allocation);

		/**
		 * Frees every block. Called before the device is destroyed, when no resource is left.
		 */
		void releaseAll(VkDevice device);

		/**
		 * The flush range for bytes [start, start + length) of an allocation: whole multiples of
		 * `atomSize` within the allocation, or up to the end of the allocation's memory, as
		 * vkFlushMappedMemoryRanges requires. Empty (size 0) for an empty range.
		 */
		static VkMappedMemoryRange flushRange(const Allocation& allocation, VkDeviceSize start, VkDeviceSize length, VkDeviceSize atomSize);

		/**
		 * What the pool has handed out, for Device::currentAllocatedSize() and Device::statistics(). These
		 * count the bytes of live allocations (what the resources and heaps asked for, rounded to the
		 * granule for pooled ones), not the memory objects the pool holds, and are safe to read from any thread.
		 */
		struct Counters {
			uint64_t liveBytes;
			uint64_t peakBytes;
			uint64_t liveAllocations;
			uint64_t totalAllocations;
			uint64_t totalBytes;
			uint64_t liveMemoryObjects;
			uint64_t totalMemoryObjects;
		};
		Counters counters() const;

	private:
		void noteAllocated(VkDeviceSize size);
		void noteFreed(VkDeviceSize size);

		std::atomic<uint64_t> _liveBytes { 0 };
		std::atomic<uint64_t> _peakBytes { 0 };
		std::atomic<uint64_t> _liveAllocations { 0 };
		std::atomic<uint64_t> _totalAllocations { 0 };
		std::atomic<uint64_t> _totalBytes { 0 };
		std::atomic<uint64_t> _liveMemoryObjects { 0 };
		std::atomic<uint64_t> _totalMemoryObjects { 0 };

		std::mutex _mutex;
		std::unordered_map<uint32_t, std::vector<std::shared_ptr<Block>>> _blocks;
	};
};
