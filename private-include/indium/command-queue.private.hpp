#pragma once

#include <indium/command-queue.hpp>

#include <vulkan/vulkan.h>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace Indium {
	/** Counts as work in progress of a command queue's threads while it exists (see waitForQueueActivity()). */
	class QueueActivity {
	public:
		QueueActivity();
		~QueueActivity();
		QueueActivity(const QueueActivity&) = delete;
		QueueActivity& operator=(const QueueActivity&) = delete;
	};

	/**
	 * Waits until no command queue thread is in the middle of submitting, calling handlers or retiring a command buffer
	 * (a thread with nothing to do does not count), or until the timeout.
	 */
	void waitForQueueActivity(std::chrono::milliseconds timeout);

	class PrivateDevice;
	class PrivateCommandBuffer;
	struct QueueWorker;
	struct TimelineSemaphore;

	class PrivateCommandQueue: public CommandQueue, public std::enable_shared_from_this<PrivateCommandQueue> {
		private:
			bool _supportsGraphics;
			bool _supportsCompute;
			std::optional<uint32_t> _queueFamilyIndex;

			// Command buffers are recorded on whatever thread the client encodes on and freed on the
			// thread that sees them complete, and Vulkan requires a command pool to be externally
			// synchronized for all of that. So each command buffer leases a pool of its own.
			std::mutex _commandPoolsMutex;
			std::vector<VkCommandPool> _idleCommandPools;

			// The command buffers in the order they were enqueued in. A command buffer is held strongly once it is
			// committed; before that the queue only watches it, so one that is dropped without being committed
			// does not stay in the queue (see PrivateCommandBuffer::~PrivateCommandBuffer()).
			struct Entry {
				PrivateCommandBuffer* key;
				std::weak_ptr<PrivateCommandBuffer> weak;
				std::shared_ptr<PrivateCommandBuffer> committed;
			};
			std::mutex _orderMutex;
			std::deque<Entry> _order;

			// the command buffers that have been submitted, in submission order: those whose scheduled handlers have not
			// been called, and those that have not been retired
			std::mutex _handlerMutex;
			std::deque<std::shared_ptr<PrivateCommandBuffer>> _toSchedule;
			std::deque<std::shared_ptr<PrivateCommandBuffer>> _inFlight;
			// the ones the GPU has not finished
			std::deque<std::shared_ptr<PrivateCommandBuffer>> _unfinished;
			bool _completionWatched = false;
			// One timeline semaphore for the queue: every submission signals the next value when it has finished.
			// Only the submission thread uses it, and the handler thread reads the value that has been reached.
			std::shared_ptr<TimelineSemaphore> _completionTimeline;

			/** Has the device's event loop call `onCompletion()` when the oldest unfinished command buffer is done; `_handlerMutex` is held. */
			void watchCompletion();
			void onCompletion();

			std::mutex _workersMutex;
			std::shared_ptr<QueueWorker> _submissionWorker;
			std::shared_ptr<QueueWorker> _handlerWorker;

			void startWorkers();
			void runHandlers();

		public:
			PrivateCommandQueue(std::shared_ptr<PrivateDevice> device);
			~PrivateCommandQueue();

			virtual std::shared_ptr<CommandBuffer> commandBuffer() override;
			virtual std::shared_ptr<Device> device() override;

			VkCommandPool acquireCommandPool();
			void releaseCommandPool(VkCommandPool commandPool);

			/** Reserves the place of a command buffer in the queue's order (`CommandBuffer::enqueue()`). */
			void enqueue(const std::shared_ptr<PrivateCommandBuffer>& commandBuffer);

			/** The command buffer is dropped without having been committed: it gives up its place. */
			void abandon(PrivateCommandBuffer* commandBuffer);

			/**
			 * Takes a committed command buffer, enqueuing it first if it has not been, and returns. The queue's
			 * submission thread submits the command buffers in the order they were enqueued in, and one that waits
			 * for an event value that has not been signaled yet holds the ones after it back (and only those:
			 * other queues go on) until the value has been reached.
			 */
			void commit(const std::shared_ptr<PrivateCommandBuffer>& commandBuffer);

			/**
			 * Submits whatever has become submittable. Runs on the submission thread, which `commit()` and the
			 * device's event loop (when an event value a command buffer waited for has been reached) wake.
			 */
			void drain();

			/** Reserves the value the timeline semaphore takes when a command buffer being submitted has finished. */
			void nextCompletion(VkSemaphore& semaphore, uint64_t& value);

			void wakeSubmission();

			/**
			 * Tells the handler thread that a command buffer has finished on the GPU, or that its scheduled handlers
			 * have returned: it calls the completed handlers of the command buffers in order and then retires them.
			 */
			void wakeHandlers();

			INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);
	};
};
