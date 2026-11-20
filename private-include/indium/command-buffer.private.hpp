#pragma once

#include <vulkan/vulkan.h>

#include <indium/command-buffer.hpp>
#include <indium/command-encoder.hpp>
#include <indium/event.private.hpp>
#include <indium/types.private.hpp>
#include <indium/hazard-tracker.private.hpp>

#include <vector>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace Indium {
	class PrivateCommandQueue;
	class PrivateDevice;
	class PipelineTimer;

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
		bool _completedHandlersFinished = false;

	public:
		PrivateCommandBuffer(std::shared_ptr<PrivateCommandQueue> commandQueue);
		~PrivateCommandBuffer();

		virtual std::shared_ptr<RenderCommandEncoder> renderCommandEncoder(const RenderPassDescriptor& descriptor) override;
		virtual std::shared_ptr<ParallelRenderCommandEncoder> parallelRenderCommandEncoder(const RenderPassDescriptor& descriptor) override;
		virtual std::shared_ptr<BlitCommandEncoder> blitCommandEncoder() override;
		virtual std::shared_ptr<BlitCommandEncoder> blitCommandEncoder(const BlitPassDescriptor& descriptor) override;
		virtual std::shared_ptr<ComputeCommandEncoder> computeCommandEncoder() override;
		virtual std::shared_ptr<ComputeCommandEncoder> computeCommandEncoder(const ComputePassDescriptor& descriptor) override;
		virtual std::shared_ptr<ComputeCommandEncoder> computeCommandEncoder(DispatchType dispatchType) override;

		virtual void encodeSignalEvent(const std::shared_ptr<Event>& event, uint64_t value) override;
		virtual void encodeWaitForEvent(const std::shared_ptr<Event>& event, uint64_t value) override;

		virtual void enqueue() override;
		virtual void commit() override;
		virtual void presentDrawable(std::shared_ptr<Drawable> drawable) override;
		virtual void addScheduledHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) override;
		virtual void addCompletedHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) override;
		virtual void waitUntilCompleted() override;

		virtual std::shared_ptr<CommandQueue> commandQueue() override;
		virtual std::shared_ptr<Device> device() override;
		virtual void waitUntilScheduled() override;

		/**
		 * Submits what can be submitted now: the batches up to the first one that waits for an event value nothing
		 * has signaled yet, for which it arranges for the queue's submission thread to be woken once the value is
		 * there. The calls to make once nothing is held (presenting the drawables) are added to `afterSubmit` when
		 * the last batch has been submitted.
		 *
		 * @returns whether the whole command buffer has been submitted.
		 */
		bool submitReady(std::vector<std::function<void()>>& afterSubmit);

		/** The GPU has finished the command buffer (called by the queue, from the device's event loop). */
		void markCompleted();
		/** The value its queue's completion timeline semaphore reaches when the GPU has finished it. */
		uint64_t completionValue() const { return _completionValue; }

		/** The GPU has finished the command buffer and its scheduled handlers have returned. */
		bool retirable();
		/** Calls the scheduled handlers; runs on the queue's handler thread once the command buffer has been submitted. */
		void runScheduledHandlers();
		/**
		 * Calls the completed handlers and releases what the command buffer held for the GPU (encoders and the
		 * resources they reference, drawables, Vulkan command buffers); runs on the queue's handler thread, for
		 * the command buffers in order.
		 */
		void retire();

		/**
		 * Keeps `resource` alive until the command buffer retires, that is until the GPU is done with the commands that
		 * refer to it, whoever else lets go of it and however long the encoder that recorded them is kept.
		 */
		void retainResource(std::shared_ptr<Resource> resource) {
			std::scoped_lock lock(_mutex);
			_retainedResources.push_back(std::move(resource));
		}

		/**
		 * Keeps `timer` until the command buffer retires, when the timestamps it wrote are read back
		 * (see PipelineTimer).
		 */
		void addPipelineTimer(std::shared_ptr<PipelineTimer> timer) {
			std::scoped_lock lock(_mutex);
			_pipelineTimers.push_back(std::move(timer));
		}

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
		void beginEncoderSegment(bool wantsPrologue = false);
		/**
		 * `hazardBarriers` are barriers the encoder needs in front of its first command: a render encoder cannot
		 * know what its draws use until it ends, and cannot take a barrier once its pass has begun, so they are
		 * recorded into a command buffer that runs before the encoder's.
		 */
		void endEncoderSegment(const std::vector<std::shared_ptr<Texture>>& used, const std::vector<std::shared_ptr<Texture>>& written, const HazardTracker::Barriers& hazardBarriers = {});

		/**
		 * Orders the commands of the command buffer by what they access (see HazardTracker). Only while
		 * hazardsTracked(): a command buffer that holds a texture that is kept equal to a buffer with copies
		 * orders every encoder with a barrier on everything instead.
		 */
		HazardTracker& hazards() { return _hazards; }
		/** Whether the encoder that was begun last is ordered by what it accesses. */
		bool hazardsTracked() const { return _hazardsTracked; }
		/** Ends an encoder of a command buffer that does not track hazards: everything it did is ordered before what follows. */
		void endUntrackedEncoder();

	private:
		VkCommandBuffer newSegment();

		/**
		 * An encodeSignalEvent() or encodeWaitForEvent(): it sits in front of the segment with the index
		 * `position`, after the segments before it.
		 */
		struct EventOperation {
			std::shared_ptr<EventCore> core;
			uint64_t value;
			bool signal;
			size_t position;
		};
		std::vector<EventOperation> _eventOperations;
		std::atomic<bool> _encoderActive { false };

		void addEventOperation(const std::shared_ptr<Event>& event, uint64_t value, bool signal);

		/**
		 * One vkQueueSubmit2 batch: the work between two event operations. A batch is submitted once the event
		 * values it waits for have been reached.
		 */
		struct SubmitBatch {
			std::vector<VkSemaphoreSubmitInfo> waits;
			std::vector<std::pair<std::shared_ptr<EventCore>, uint64_t>> eventWaits;
			std::vector<VkCommandBufferSubmitInfo> commandBuffers;
			// the event signals this batch ends with (the first ones of `signals`, once it is submitted)
			std::vector<std::pair<std::shared_ptr<EventCore>, uint64_t>> eventSignals;
			std::vector<VkSemaphoreSubmitInfo> signals;
			// a signal has been added; nothing recorded after it joins the batch
			bool closed = false;
		};

		struct Submission {
			std::vector<SubmitBatch> batches;
			// the first batch not submitted yet
			size_t next = 0;
			bool waiterRegistered = false;
			std::vector<std::shared_ptr<Texture>> readWriteTextures;
			std::vector<std::shared_ptr<BinarySemaphore>> presentationSemaphores;
		};
		std::unique_ptr<Submission> _submission;

		void prepareSubmission();
		void freeSegments(VkCommandPool pool, const std::vector<VkCommandBuffer>& segments);
		/** Runs on the submission thread right after the last batch has been submitted. */
		void finishSubmission(const std::vector<std::shared_ptr<Texture>>& readWriteTextures, const std::vector<std::shared_ptr<BinarySemaphore>>& presentationSemaphores);

		uint64_t _completionValue = 0;
		std::vector<std::shared_ptr<BinarySemaphore>> _retainedSemaphores;
		std::vector<std::shared_ptr<Resource>> _retainedResources;
		std::vector<std::shared_ptr<PipelineTimer>> _pipelineTimers;

		VkCommandPool _commandPool = VK_NULL_HANDLE;
		// the finished segments in execution order (`_commandBuffer` is the one being recorded), and every one allocated
		std::vector<VkCommandBuffer> _finishedSegments;
		std::vector<VkCommandBuffer> _allSegments;
		bool _encoderSegmentOpen = false;
		HazardTracker _hazards;
		bool _hazardsTracked = true;

	private:
		std::condition_variable _scheduledCondvar;
		bool _scheduledHandlersFinished = false;
	};
};
