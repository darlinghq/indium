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
		virtual std::shared_ptr<BlitCommandEncoder> blitCommandEncoder(const BlitPassDescriptor& descriptor) override;
		virtual std::shared_ptr<ComputeCommandEncoder> computeCommandEncoder() override;
		virtual std::shared_ptr<ComputeCommandEncoder> computeCommandEncoder(const ComputePassDescriptor& descriptor) override;
		virtual std::shared_ptr<ComputeCommandEncoder> computeCommandEncoder(DispatchType dispatchType) override;

		virtual void commit() override;
		virtual void presentDrawable(std::shared_ptr<Drawable> drawable) override;
		virtual void addScheduledHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) override;
		virtual void addCompletedHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) override;
		virtual void waitUntilCompleted() override;

		virtual std::shared_ptr<CommandQueue> commandQueue() override;
		virtual std::shared_ptr<Device> device() override;
		virtual void waitUntilScheduled() override;

		void addScheduledHandlerLocked(std::function<void(std::shared_ptr<CommandBuffer>)> handler);
		void addCompletedHandlerLocked(std::function<void(std::shared_ptr<CommandBuffer>)> handler);

		INDIUM_PROPERTY_READONLY_OBJECT(PrivateCommandQueue, p, P,rivateCommandQueue);
		INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);

		INDIUM_PROPERTY(VkCommandBuffer, c, C,ommandBuffer) = VK_NULL_HANDLE;

	public:
		/**
		 * A texture that is an image of its own kept equal to a buffer's rows (ConcreteTexture::BufferBacking) needs
		 * copies recorded outside of any render pass: the buffer into the image before an encoder that uses it, and
		 * the image into the buffer after one that writes it. Once the device has such a texture, every encoder is
		 * recorded into a command buffer of its own (a segment) and these two are called around it: the first
		 * before the encoder is created, the second when it ends, with the textures it used and wrote. The
		 * segments are submitted in order, with the copies in between.
		 */
		void beginEncoderSegment();
		void endEncoderSegment(const std::vector<std::shared_ptr<Texture>>& used, const std::vector<std::shared_ptr<Texture>>& written);

	private:
		VkCommandBuffer newSegment();

		VkCommandPool _commandPool = VK_NULL_HANDLE;
		// the finished segments in execution order (`_commandBuffer` is the one being recorded), and every one allocated
		std::vector<VkCommandBuffer> _finishedSegments;
		std::vector<VkCommandBuffer> _allSegments;
		bool _encoderSegmentOpen = false;

	private:
		std::condition_variable _scheduledCondvar;
		bool _scheduledHandlersFinished = false;
	};
};
