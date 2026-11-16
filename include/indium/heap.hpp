#pragma once

// Copyright (C) 2026 Lubos Dolezel

#include <indium/base.hpp>
#include <indium/types.hpp>
#include <indium/texture.hpp>

#include <cstddef>
#include <memory>

namespace Indium {
	class Device;
	class Buffer;

	enum class HeapType {
		/** The heap decides where each resource goes. */
		Automatic = 0,
		/** The caller decides: every resource is created at an offset, and resources may overlap (alias). */
		Placement = 1,
		/** Not supported: Device::newHeap() refuses it. */
		Sparse = 2,
	};

	struct HeapDescriptor {
		size_t size = 0;
		// Shared and Private are supported; Managed and Memoryless heaps are refused.
		StorageMode storageMode = StorageMode::Private;
		CPUCacheMode cpuCacheMode = CPUCacheMode::DefaultCache;
		/** Default means untracked for a heap. */
		HazardTrackingMode hazardTrackingMode = HazardTrackingMode::HazardTrackingModeDefault;
		HeapType type = HeapType::Automatic;
	};

	struct SizeAndAlign {
		size_t size = 0;
		size_t align = 0;
	};

	/**
	 * A block of device memory that buffers and textures are made from. The memory is allocated when the
	 * heap is created. Resources keep their heap alive, so it is freed after the last of them.
	 *
	 * A resource takes `Device::heapBufferSizeAndAlign()` / `heapTextureSizeAndAlign()` bytes, at an offset that is
	 * a multiple of the alignment those return. Both sizes include the device's own rounding.
	 */
	class Heap {
	public:
		virtual ~Heap() = 0;

		virtual std::shared_ptr<Device> device() = 0;

		virtual HeapType type() const = 0;
		virtual StorageMode storageMode() const = 0;
		virtual CPUCacheMode cpuCacheMode() const = 0;
		/** Untracked or Tracked; a descriptor's Default is reported as Untracked. */
		virtual HazardTrackingMode hazardTrackingMode() const = 0;

		/** The size of the heap's memory: the descriptor's size rounded up to a whole number of pages. */
		virtual size_t size() const = 0;

		/**
		 * The sum of the sizes (as `Device::heapBufferSizeAndAlign()` and `heapTextureSizeAndAlign()` return them) of the
		 * resources made from an automatic heap that have not been released or made aliasable. Always 0 for a placement heap.
		 */
		virtual size_t usedSize() const = 0;

		/** The memory the heap has allocated: all of it, from the moment the heap exists. */
		virtual size_t currentAllocatedSize() const = 0;

		/**
		 * The size of the largest resource an automatic heap could place now at an offset that is a multiple of
		 * `alignment` (0 for a placement heap, which places nothing itself).
		 */
		virtual size_t maxAvailableSize(size_t alignment) const = 0;

		/**
		 * Resources from an automatic heap. `options` must have the heap's storage mode and CPU cache mode (its
		 * hazard tracking mode is not used: the resource has the heap's). Returns `nullptr` if the heap has no room,
		 * is a placement heap, or the options do not match. A buffer of length 0 is refused.
		 */
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options) = 0;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor) = 0;

		/**
		 * Resources from a placement heap, at a byte offset into the heap that is a multiple of the resource's
		 * alignment and leaves the resource inside the heap. Resources may overlap; what one of them holds is
		 * undefined after another that overlaps it has been written. Returns `nullptr` for an automatic heap, an
		 * unaligned or out-of-range offset, or options that do not match the heap's.
		 */
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options, size_t offset) = 0;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor, size_t offset) = 0;
	};
};
