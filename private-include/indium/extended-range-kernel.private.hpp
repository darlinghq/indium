// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <indium/base.hpp>
#include <indium/extended-range.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <vulkan/vulkan.h>

namespace Indium {
	class PrivateDevice;

	/**
	 * The compute pass over the image of a texture in an extended-range format (see extended-range.private.hpp):
	 * it rounds what a render pass wrote to the values a code can hold, and turns the gamma encoded values of a
	 * non-sRGB format into linear light for presentation. It is plain Vulkan, with a descriptor set per use that
	 * the caller allocates, so that it can run inside any command buffer.
	 */
	class ExtendedRangeKernel {
		INDIUM_PREVENT_COPY(ExtendedRangeKernel);

	private:
		VkDevice _device;
		VkDescriptorSetLayout _setLayout = VK_NULL_HANDLE;
		VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
		VkShaderModule _module = VK_NULL_HANDLE;
		VkPipeline _pipeline = VK_NULL_HANDLE;

	public:
		/** What a pass over the image does. */
		enum class Mode: uint32_t {
			Round = 0,
			RoundWithoutAlpha = 1,
			RoundSRGB = 2,
			RoundSRGBWithoutAlpha = 3,
			EncodedToLinear = 4,
		};

		/** The rounding pass of a format. */
		static Mode roundingMode(PixelFormat pixelFormat) {
			bool alpha = ExtendedRange::hasAlpha(pixelFormat);
			if (ExtendedRange::isSRGB(pixelFormat)) {
				return alpha ? Mode::RoundSRGB : Mode::RoundSRGBWithoutAlpha;
			}
			return alpha ? Mode::Round : Mode::RoundWithoutAlpha;
		};

		explicit ExtendedRangeKernel(PrivateDevice& device);
		~ExtendedRangeKernel();

		VkDescriptorSetLayout setLayout() const { return _setLayout; }

		/**
		 * Records the pass over `view` (an RGBA32F view of one level and layer of a storage image in the general
		 * layout) of `width` by `height` texels. `set` is a descriptor set of setLayout(), which is written here.
		 * The caller orders the pass against the work before and after it.
		 */
		void record(VkCommandBuffer commandBuffer, VkDescriptorSet set, VkImageView view, uint32_t width, uint32_t height, Mode mode);
	};
};
