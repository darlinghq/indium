// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <indium/counter.hpp>

#include <vulkan/vulkan.h>

#include <memory>

namespace Indium {
	class PrivateDevice;

	/**
	 * A counter sample buffer for the timestamp counter set, backed by a Vulkan timestamp query pool.
	 */
	class PrivateCounterSampleBuffer: public CounterSampleBuffer {
	private:
		std::shared_ptr<PrivateDevice> _device;
		VkQueryPool _pool = VK_NULL_HANDLE;
		size_t _sampleCount;

	public:
		PrivateCounterSampleBuffer(std::shared_ptr<PrivateDevice> device, VkQueryPool pool, size_t sampleCount);
		~PrivateCounterSampleBuffer();

		virtual std::shared_ptr<Device> device() override;
		virtual size_t sampleCount() override;
		virtual std::vector<char> resolveCounterRange(Range<size_t> range) override;

		/**
		 * Records a timestamp into sample `index` at `stage`, replacing any earlier value. It must be
		 * called outside a render pass, because the sample is reset first. An index at or past the
		 * sample count, or counterDontSample, records nothing.
		 */
		void record(VkCommandBuffer commandBuffer, size_t index, VkPipelineStageFlagBits stage);

		/**
		 * Records `buffer`'s sample at `index` if `buffer` is one of ours; anything else records nothing.
		 */
		static void record(const std::shared_ptr<CounterSampleBuffer>& buffer, VkCommandBuffer commandBuffer, size_t index, VkPipelineStageFlagBits stage);
	};
};
