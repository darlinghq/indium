#pragma once

#include <indium/types.hpp>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace Indium {
	// Shared and Managed resources need memory the CPU can map (coherent for Shared, whose writes Metal
	// does not ask to be flushed), and take the first such type: moving them into DEVICE_LOCAL and
	// HOST_VISIBLE (BAR) memory would make their CPU reads uncached. Private and Memoryless resources are
	// only touched by the GPU, so they take the first DEVICE_LOCAL type. The Vulkan specification orders
	// memory types so that a type whose properties are a subset of another's comes first, so the first
	// allowed type can be one without DEVICE_LOCAL (system memory the GPU reads over the bus) even where
	// device memory is available; that one is only the fallback.
	inline size_t chooseMemoryType(const VkPhysicalDeviceMemoryProperties& memory, uint32_t memoryTypeBits, StorageMode storageMode) {
		bool hostAccess = storageMode == StorageMode::Shared || storageMode == StorageMode::Managed;
		size_t firstFit = SIZE_MAX;

		for (size_t i = 0; i < memory.memoryTypeCount; ++i) {
			auto flags = memory.memoryTypes[i].propertyFlags;

			if ((memoryTypeBits & (1u << i)) == 0) {
				continue;
			}
			if (hostAccess && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
				continue;
			}
			if (storageMode == StorageMode::Shared && (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
				continue;
			}

			if (hostAccess || (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
				return i;
			}
			if (firstFit == SIZE_MAX) {
				firstFit = i;
			}
		}

		return firstFit;
	}
};
