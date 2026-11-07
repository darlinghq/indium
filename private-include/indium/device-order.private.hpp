// Copyright (C) 2026 Lubos Dolezel

#pragma once

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Indium {
	/**
	 * How strongly a Vulkan device type is preferred as the default device: discrete GPUs first (on a
	 * Mac with several GPUs the default device is the discrete one), then integrated and virtual
	 * GPUs, then everything else.
	 */
	inline int deviceTypeRank(VkPhysicalDeviceType type) {
		switch (type) {
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 3;
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 2;
			case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 1;
			default:                                     return 0; // CPU, or a type we do not know
		}
	}

	/**
	 * The order in which the usable devices of the given types (in the loader's enumeration order) are
	 * offered, as indices into `types`. The first one is the default device.
	 *
	 * Devices are ranked by deviceTypeRank(), keeping the loader's order among devices of equal rank.
	 * A Mac has no software Metal device, so a CPU rasterizer (llvmpipe, lavapipe, SwiftShader) is only
	 * offered when there is no GPU at all.
	 */
	inline std::vector<size_t> orderDevices(const std::vector<VkPhysicalDeviceType>& types) {
		const bool haveGPU = std::any_of(types.begin(), types.end(), [](VkPhysicalDeviceType type) {
			return deviceTypeRank(type) > 0;
		});

		std::vector<size_t> order;
		for (size_t i = 0; i < types.size(); ++i) {
			if (haveGPU && types[i] == VK_PHYSICAL_DEVICE_TYPE_CPU) {
				continue;
			}
			order.push_back(i);
		}

		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			return deviceTypeRank(types[a]) > deviceTypeRank(types[b]);
		});

		return order;
	}
};
