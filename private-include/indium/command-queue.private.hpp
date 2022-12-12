#pragma once

#include <indium/command-queue.hpp>

#include <vulkan/vulkan.h>

namespace Indium {
	class PrivateDevice;

	class PrivateCommandQueue: public CommandQueue, public std::enable_shared_from_this<PrivateCommandQueue> {
		private:
			bool _supportsGraphics;
			bool _supportsCompute;

		public:
			PrivateCommandQueue(std::shared_ptr<PrivateDevice> device);
			~PrivateCommandQueue();

			virtual std::shared_ptr<CommandBuffer> commandBuffer() override;
			virtual std::shared_ptr<Device> device() override;

			INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);

			INDIUM_PROPERTY(VkCommandPool, c, C,ommandPool) = VK_NULL_HANDLE;
	};
};
