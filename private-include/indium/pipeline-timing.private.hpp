// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <indium/device.hpp>

#include <vulkan/vulkan.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Indium {
	class PrivateDevice;

	/**
	 * The totals PipelineTimer spans are added to, kept by the device (Device::pipelineTimings()).
	 */
	class PipelineTimingTable {
	public:
		void add(const std::string& name, uint64_t nanoseconds);
		std::vector<PipelineTiming> snapshot() const;

	private:
		mutable std::mutex _mutex;
		std::map<std::string, PipelineTiming> _timings;
	};

	/**
	 * Timestamps around the spans of GPU work one encoder records, made only while pipeline timing is enabled
	 * on the device (Device::setPipelineTimingEnabled()). A span is two Vulkan timestamps (top and bottom of
	 * the pipe) with the name of the pipeline that ran in between. The command buffer keeps the timer until it
	 * retires, when the GPU is done and `resolve()` adds the spans to the device's table.
	 *
	 * Query pools are reset from the host when they are made, as the number of spans is only known while
	 * encoding and a pool cannot be reset inside a render pass; Device::supportsPipelineTiming() says whether
	 * the device allows that.
	 */
	class PipelineTimer {
	public:
		explicit PipelineTimer(std::shared_ptr<PrivateDevice> device);
		~PipelineTimer();

		/** Ends the open span, if any, and starts a span named `name`. */
		void begin(VkCommandBuffer commandBuffer, const std::string& name);

		/** Ends the open span, if any. */
		void end(VkCommandBuffer commandBuffer);

		/** Reads the timestamps back, once the GPU has finished, and adds the spans to the device's table. */
		void resolve();

	private:
		static constexpr uint32_t queriesPerPool = 256;

		struct Span {
			std::string name;
			uint32_t first;
			uint32_t last;
			bool closed;
		};

		uint32_t allocateQuery(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits stage);

		std::shared_ptr<PrivateDevice> _device;
		std::vector<VkQueryPool> _pools;
		uint32_t _queryCount = 0;
		std::vector<Span> _spans;
	};
};
