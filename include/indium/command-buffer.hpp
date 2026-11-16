#pragma once

#include <memory>
#include <functional>

#include <indium/render-command-encoder.hpp>
#include <indium/blit-command-encoder.hpp>
#include <indium/compute-command-encoder.hpp>
#include <indium/base.hpp>
#include <indium/event.hpp>

namespace Indium {
	class CommandQueue;
	class Device;
	class Drawable;

	class CommandBuffer {
	public:
		virtual ~CommandBuffer() = 0;

		virtual std::shared_ptr<RenderCommandEncoder> renderCommandEncoder(const RenderPassDescriptor& descriptor) = 0;
		virtual std::shared_ptr<BlitCommandEncoder> blitCommandEncoder() = 0;
		virtual std::shared_ptr<BlitCommandEncoder> blitCommandEncoder(const BlitPassDescriptor& descriptor) = 0;
		virtual std::shared_ptr<ComputeCommandEncoder> computeCommandEncoder() = 0;
		virtual std::shared_ptr<ComputeCommandEncoder> computeCommandEncoder(const ComputePassDescriptor& descriptor) = 0;
		virtual std::shared_ptr<ComputeCommandEncoder> computeCommandEncoder(DispatchType dispatchType) = 0;

		/**
		 * Makes `event` reach `value` once the work encoded before this call (and everything committed before this
		 * command buffer on its queue) has finished. A value the event has already reached or passed leaves it
		 * unchanged. Not allowed while an encoder is active (`std::logic_error`); the event must be one of this
		 * device (`std::invalid_argument`).
		 */
		virtual void encodeSignalEvent(const std::shared_ptr<Event>& event, uint64_t value) = 0;

		/**
		 * Holds back the work encoded after this call, and the command buffers committed after this one on its
		 * queue, until `event` has reached `value`. A value it already reached holds nothing back. Same
		 * restrictions as `encodeSignalEvent()`.
		 */
		virtual void encodeWaitForEvent(const std::shared_ptr<Event>& event, uint64_t value) = 0;

		virtual void commit() = 0;
		virtual void presentDrawable(std::shared_ptr<Drawable> drawable) = 0;

		using Handler = std::function<void(std::shared_ptr<CommandBuffer>)>;
		virtual void addScheduledHandler(Handler handler) = 0;
		virtual void addCompletedHandler(Handler handler) = 0;
		virtual void waitUntilCompleted() = 0;

		virtual std::shared_ptr<CommandQueue> commandQueue() = 0;
		virtual std::shared_ptr<Device> device() = 0;
		virtual void waitUntilScheduled() = 0;
	};
};
