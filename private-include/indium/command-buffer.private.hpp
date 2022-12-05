#pragma once

#include <vulkan/vulkan.h>

#include <indium/command-buffer.hpp>
#include <indium/command-encoder.hpp>

#include <vector>
#include <mutex>
#include <condition_variable>

namespace Indium {
	class PrivateCommandQueue;
	class PrivateDevice;

	class PrivateCommandBuffer: public CommandBuffer, public std::enable_shared_from_this<PrivateCommandBuffer> {
	private:
		std::mutex _mutex;
		// we have to keep some encoders (e.g. render encoders) alive until the buffer has finished executing.
		// the encoders themselves aren't too important, but the resources they reference are.
		std::vector<std::shared_ptr<CommandEncoder>> _commandEncoders;
		std::vector<std::shared_ptr<Drawable>> _drawablesToPresent;
		std::vector<Handler> _scheduledHandlers;
		std::vector<Handler> _completedHandlers;
		bool _committed = false;
		std::condition_variable _completedCondvar;
		bool _completed = false;

	public:
		PrivateCommandBuffer(std::shared_ptr<PrivateCommandQueue> commandQueue);
		~PrivateCommandBuffer();

		virtual std::shared_ptr<RenderCommandEncoder> renderCommandEncoder(const RenderPassDescriptor& descriptor) override;
		virtual std::shared_ptr<BlitCommandEncoder> blitCommandEncoder() override;
		virtual std::shared_ptr<BlitCommandEncoder> blitCommandEncoderWithDescriptor(const BlitPassDescriptor& descriptor) override;

		virtual void commit() override;
		virtual void presentDrawable(std::shared_ptr<Drawable> drawable) override;
		virtual void addScheduledHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) override;
		virtual void addCompletedHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) override;
		virtual void waitUntilCompleted() override;

		virtual std::shared_ptr<CommandQueue> commandQueue() override;
		virtual std::shared_ptr<Device> device() override;

		INDIUM_PROPERTY_READONLY_OBJECT(PrivateCommandQueue, p, P,rivateCommandQueue);
		INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);

		INDIUM_PROPERTY(VkCommandBuffer, c, C,ommandBuffer) = nullptr;
	};
};
