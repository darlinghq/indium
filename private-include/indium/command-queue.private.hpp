#pragma once

#include <indium/command-queue.hpp>

#include <vulkan/vulkan.h>

#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace Indium {
	class PrivateDevice;
	class PrivateCommandBuffer;

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

			std::mutex _pendingMutex;
			std::deque<std::shared_ptr<PrivateCommandBuffer>> _pending;
			// held while the pending command buffers are submitted, one thread at a time
			std::mutex _drainMutex;

		public:
			PrivateCommandQueue(std::shared_ptr<PrivateDevice> device);
			~PrivateCommandQueue();

			virtual std::shared_ptr<CommandBuffer> commandBuffer() override;
			virtual std::shared_ptr<Device> device() override;

			VkCommandPool acquireCommandPool();
			void releaseCommandPool(VkCommandPool commandPool);

			/**
			 * Takes a committed command buffer. Command buffers of a queue are submitted in the order they were
			 * committed in, and one that waits for an event value that has not been signaled yet holds the ones after
			 * it back (and only those: other queues go on) until the value has been reached.
			 */
			void submit(std::shared_ptr<PrivateCommandBuffer> commandBuffer);

			/**
			 * Submits whatever has become submittable. Called by `submit()` and, from the device's event loop, when
			 * an event value a command buffer waited for has been reached.
			 */
			void drain();

			INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);
	};
};
