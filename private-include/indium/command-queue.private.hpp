#pragma once

#include <indium/command-queue.hpp>

#include <vulkan/vulkan.h>

#include <mutex>
#include <optional>
#include <vector>

namespace Indium {
	class PrivateDevice;

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

		public:
			PrivateCommandQueue(std::shared_ptr<PrivateDevice> device);
			~PrivateCommandQueue();

			virtual std::shared_ptr<CommandBuffer> commandBuffer() override;
			virtual std::shared_ptr<Device> device() override;

			VkCommandPool acquireCommandPool();
			void releaseCommandPool(VkCommandPool commandPool);

			INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);
	};
};
