// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <indium/render-command-encoder.hpp>
#include <indium/buffer.private.hpp>

#include <vulkan/vulkan.h>

#include <memory>
#include <unordered_set>
#include <vector>

namespace Indium {
	class PrivateDevice;
	class PrivateCommandBuffer;
	class PrivateComputeCommandEncoder;

	/**
	 * The visibility results of one render encoder (RenderPassDescriptor::visibilityResultBuffer and
	 * RenderCommandEncoder::setVisibilityResultMode()).
	 *
	 * Every call that sets a mode other than Disabled starts a span: an occlusion query that counts the samples
	 * of the draws up to the next call, and ends the span before it. When the render pass ends, the results of
	 * the queries are copied into a scratch buffer and a one-thread compute dispatch folds them into the result
	 * buffer, in the order the spans were set, so the buffer is written when the command buffer executes.
	 *
	 * A slot (8 bytes at the offset of a span) is cleared by the first span of the encoder that uses it. A
	 * counting span adds its sample count to the slot, a boolean span sets the slot to 1 if any sample passed.
	 * Slots no span uses are not touched.
	 *
	 * Occlusion queries cannot be begun and ended in separate render pass instances, so a span never reaches past
	 * the encoder; query pools are reset from the host when they are made, which needs hostQueryReset (without
	 * it, `supported()` is false and the encoder ignores the calls).
	 */
	class VisibilityResults {
	public:
		static constexpr size_t slotSize = 8;

		static bool supported(const std::shared_ptr<PrivateDevice>& device);

		VisibilityResults(std::shared_ptr<PrivateDevice> device, std::shared_ptr<PrivateBuffer> buffer);
		~VisibilityResults();

		/** Ends the active span, if any, and starts one for `mode` at `offset` unless the mode is Disabled. */
		void setMode(VkCommandBuffer commandBuffer, VisibilityResultMode mode, size_t offset);

		/** Ends the active span. Called before the render pass ends. */
		void end(VkCommandBuffer commandBuffer);

		/**
		 * Records the copy of the query results and the dispatch that writes the result buffer into the command
		 * buffer, after the render pass has ended. Does nothing if no span was started.
		 */
		void resolve(const std::shared_ptr<PrivateCommandBuffer>& commandBuffer);

	private:
		static constexpr uint32_t queriesPerPool = 256;

		struct Span {
			uint32_t slot;
			uint32_t mode;
			uint32_t query;
			uint32_t first;
		};

		std::shared_ptr<PrivateDevice> _device;
		std::shared_ptr<PrivateBuffer> _buffer;
		std::vector<VkQueryPool> _pools;
		uint32_t _queryCount = 0;
		bool _active = false;
		std::vector<Span> _spans;
		std::unordered_set<uint32_t> _usedSlots;
		// the dispatch that writes the result buffer: its pipelines and scratch buffers live as long as we do
		std::shared_ptr<PrivateComputeCommandEncoder> _resolver;
	};
};
