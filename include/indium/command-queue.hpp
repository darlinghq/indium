#pragma once

#include <indium/command-buffer.hpp>
#include <indium/base.hpp>

namespace Indium {
	class Device;

	class CommandQueue {
	public:
		virtual ~CommandQueue() = 0;

		virtual std::shared_ptr<CommandBuffer> commandBuffer() = 0;

		virtual std::shared_ptr<Device> device() = 0;
	};
};
