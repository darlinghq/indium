#pragma once

#include <vulkan/vulkan.h>

#include <indium/device.hpp>
#include <indium/types.private.hpp>

#include <vector>
#include <mutex>
#include <functional>
#include <atomic>
#include <optional>

namespace Indium {
	class PrivateDevice;

	extern std::vector<std::shared_ptr<PrivateDevice>> globalDeviceList;

	void initGlobalDeviceList();
	void finitGlobalDeviceList();

	class PrivateDevice: public Device, public std::enable_shared_from_this<PrivateDevice> {
	private:

		std::mutex _eventLoopMutex;
		std::mutex _pollingMutex;

		// these are stored separately (rather than in a single structure)
		// so that we can easily copy them individually in the event loop polling function.
		// index 0 is reserved for the wakeup semaphore.
		std::vector<VkSemaphore> _eventLoopSemaphores;
		std::vector<uint64_t> _eventLoopWaitValues;
		std::vector<std::function<void()>> _eventLoopCallbacks;

	public:
		PrivateDevice(VkPhysicalDevice physicalDevice);
		~PrivateDevice();

		virtual std::string name() const override;
		virtual std::shared_ptr<CommandQueue> newCommandQueue() override;
		virtual std::shared_ptr<RenderPipelineState> newRenderPipelineState(const RenderPipelineDescriptor& descriptor) override;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(const ComputePipelineDescriptor& descriptor, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) override;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(std::shared_ptr<Function> computeFunction, PipelineOption options = PipelineOption::None, std::shared_ptr<ComputePipelineReflection> reflection = nullptr) override;
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options) override;
		virtual std::shared_ptr<Buffer> newBuffer(const void* pointer, size_t length, ResourceOptions options) override;
		virtual std::shared_ptr<Library> newLibrary(const void* data, size_t length) override;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor) override;
		virtual std::shared_ptr<SamplerState> newSamplerState(const SamplerDescriptor& descriptor) override;
		virtual std::shared_ptr<DepthStencilState> newDepthStencilState(const DepthStencilDescriptor& descriptor) override;

		virtual void pollEvents(uint64_t timeoutNanoseconds) override;
		virtual void wakeupEventLoop() override;

		void waitForSemaphore(VkSemaphore semaphore, uint64_t targetValue, std::function<void()> callback);

		/**
		 * @note This method MAY return a previously-used semaphore, so the count may not
		 *       always be 0. Be sure to use the count value of the returned structure.
		 */
		TimelineSemaphore getTimelineSemaphore();
		void putTimelineSemaphore(const TimelineSemaphore& semaphore);

		/**
		 * Similar to getTimelineSemaphore(), but wraps it in a shared pointer
		 * that automatically releases the semaphore when all references to it die out.
		 */
		std::shared_ptr<TimelineSemaphore> getWrappedTimelineSemaphore();

		BinarySemaphore getBinarySemaphore(bool exportable = false);
		void putBinarySemaphore(const BinarySemaphore& semaphore);

		std::shared_ptr<BinarySemaphore> getWrappedBinarySemaphore(bool exportable = false);

		INDIUM_PROPERTY(VkPhysicalDevice, p, P,hysicalDevice) = VK_NULL_HANDLE;
		INDIUM_PROPERTY(VkPhysicalDeviceProperties, p, P,roperties);
		INDIUM_PROPERTY(VkDevice, d, D,evice) = VK_NULL_HANDLE;
		INDIUM_PROPERTY(std::optional<uint32_t>, g, G,raphicsQueueFamilyIndex);
		INDIUM_PROPERTY(std::optional<uint32_t>, c, C,omputeQueueFamilyIndex);
		INDIUM_PROPERTY(std::optional<uint32_t>, t, T,ransferQueueFamilyIndex);
		// in theory, the presentation queue *can* be different from the graphics queue,
		// but in practice, they're actually the same.
		// TODO: maybe implement this just in case?
		//INDIUM_PROPERTY(std::optional<uint32_t>, p, P,resentQueueFamilyIndex);
		INDIUM_PROPERTY(VkQueue, g, G,raphicsQueue) = VK_NULL_HANDLE;
		INDIUM_PROPERTY(VkQueue, c, C,omputeQueue) = VK_NULL_HANDLE;
		INDIUM_PROPERTY(VkQueue, t, T,ransferQueue) = VK_NULL_HANDLE;
		//INDIUM_PROPERTY(VkQueue, p, P,resentQueue) = VK_NULL_HANDLE;
		INDIUM_PROPERTY(VkCommandPool, o,O,neshotCommandPool) = VK_NULL_HANDLE;

		INDIUM_PROPERTY(VkPhysicalDeviceMemoryProperties, m, M,emoryProperties);
	};
};
