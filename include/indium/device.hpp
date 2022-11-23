#pragma once

#include <string>
#include <memory>

#include <indium/init.hpp>
#include <indium/base.hpp>
#include <indium/types.hpp>

namespace Indium {
	class CommandQueue;
	class RenderPipelineState;
	struct RenderPipelineDescriptor;
	class Buffer;
	class Library;

	class Device {
	public:
		virtual ~Device() = 0;

		virtual std::string name() const = 0;

		virtual std::shared_ptr<CommandQueue> newCommandQueue() = 0;
		virtual std::shared_ptr<RenderPipelineState> newRenderPipelineState(const RenderPipelineDescriptor& descriptor) = 0;
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options) = 0;
		virtual std::shared_ptr<Buffer> newBuffer(const void* pointer, size_t length, ResourceOptions options) = 0;
		virtual std::shared_ptr<Library> newLibrary(const void* data, size_t length) = 0;

		// --- support api ---

		/**
		 * @note This method should only be called from a single thread at a time.
		 *       It *can* be called by different threads, but not simultaneously.
		 *       If it is called by multiple threads simultaneously, only one will
		 *       poll at a time; all others will be left waiting to poll.
		 */
		virtual void pollEvents(uint64_t timeoutNanoseconds) = 0;
		virtual void wakeupEventLoop() = 0;
	};

	std::shared_ptr<Device> createSystemDefaultDevice();
};
