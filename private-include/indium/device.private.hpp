#pragma once

#include "indium/base.hpp"
#include <cstdint>
#include <type_traits>
#include <vulkan/vulkan.h>

#include <indium/device.hpp>
#include <indium/types.private.hpp>
#include <indium/render-pass-key.private.hpp>
#include <indium/memory-pool.private.hpp>

#include <vector>
#include <mutex>
#include <condition_variable>
#include <set>
#include <functional>
#include <atomic>
#include <optional>
#include <unordered_map>

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

		// waitForSemaphore() only needs to wake the event loop when a poller is blocked on a list
		// that predates the new semaphore. Registrations count additions to the list; a poller
		// counts itself in blockedPollers before it blocks, then rechecks registrations.
		std::atomic<uint64_t> _eventLoopRegistrations { 0 };
		std::atomic<uint32_t> _eventLoopBlockedPollers { 0 };

		// Vulkan requires submissions and presentation on one queue to be externally synchronized
		std::mutex _graphicsQueueMutex;
		std::mutex _oneshotCommandPoolMutex;

		// The Vulkan device is created the first time something needs it (see device()), so that
		// listing devices, or choosing one, costs no VkDevice for the devices left unused.
		std::mutex _logicalDeviceMutex;
		std::condition_variable _logicalDeviceCondition;
		std::atomic<bool> _logicalDeviceCreated { false };
		bool _wakeupBeforeCreation = false;
		std::set<uint32_t> _queueFamilyIndices;
		std::vector<const char*> _enabledExtensions;

		VkDevice _device = VK_NULL_HANDLE;
		VkQueue _graphicsQueue = VK_NULL_HANDLE;
		VkQueue _computeQueue = VK_NULL_HANDLE;
		VkQueue _transferQueue = VK_NULL_HANDLE;
		VkCommandPool _oneshotCommandPool = VK_NULL_HANDLE;

		void createLogicalDevice();

		void ensureLogicalDevice() const {
			if (!_logicalDeviceCreated.load(std::memory_order_acquire)) {
				// creating the Vulkan device on first use does not change what this device is
				const_cast<PrivateDevice*>(this)->createLogicalDevice();
			}
		}

		std::mutex _fallbackMutex;
		std::shared_ptr<Buffer> _fallbackBuffer;
		std::shared_ptr<SamplerState> _fallbackSampler;
		bool _nullDescriptor = false;

	public:
		PrivateDevice(VkPhysicalDevice physicalDevice);
		~PrivateDevice();

		enum class Feature: uint64_t {
			Swapchain           = 1 << 0,
			ExternalMemoryFD    = 1 << 1,
			ExternalSemaphoreFD = 1 << 2,
			NonSemanticInfo     = 1 << 3,
			MemoryBudget        = 1 << 4,
			ExternalMemoryDMABuf = 1 << 5,
			ImageDRMFormatModifier = 1 << 6,
		};

		friend inline Feature operator|(Feature lhs, Feature rhs) {
			return static_cast<Feature>(static_cast<std::underlying_type_t<Feature>>(lhs) | static_cast<std::underlying_type_t<Feature>>(rhs));
		}

		friend inline Feature operator&(Feature lhs, Feature rhs) {
			return static_cast<Feature>(static_cast<std::underlying_type_t<Feature>>(lhs) & static_cast<std::underlying_type_t<Feature>>(rhs));
		}

		friend inline bool operator!(Feature features) {
			return features == static_cast<Feature>(0);
		}

		virtual std::string name() const override;
		virtual uint64_t recommendedMaxWorkingSetSize() const override;
		virtual uint64_t maxBufferLength() const override;
		virtual uint32_t vendorID() const override;
		virtual uint32_t deviceID() const override;
		virtual DeviceType deviceType() const override;
		virtual std::vector<MemoryHeap> memoryHeaps() const override;
		virtual std::array<uint8_t, 16> deviceUUID() const override;
		virtual std::optional<uint32_t> memoryTypeIndex(uint32_t memoryTypeBits, StorageMode storageMode) const override;
		virtual bool supportsDepth24Stencil8PixelFormat() const override;
		virtual bool supportsPixelFormatSampling(PixelFormat format, bool filtered) const override;
		virtual bool supportsTextureSampleCount(uint32_t sampleCount) const override;
		virtual std::shared_ptr<CommandQueue> newCommandQueue() override;
		virtual std::shared_ptr<RenderPipelineState> newRenderPipelineState(const RenderPipelineDescriptor& descriptor) override;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(const ComputePipelineDescriptor& descriptor, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) override;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(std::shared_ptr<Function> computeFunction, PipelineOption options = PipelineOption::None, std::shared_ptr<ComputePipelineReflection> reflection = nullptr) override;
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options) override;
		virtual std::shared_ptr<Buffer> newBuffer(const void* pointer, size_t length, ResourceOptions options) override;
		virtual std::shared_ptr<Library> newLibrary(const void* data, size_t length) override;
		virtual std::shared_ptr<Library> newLibrary(const void* spirv, size_t length, const LibraryReflection& reflection, std::string* errorMessage = nullptr) override;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor) override;
		virtual bool supportsDMABufImport() const override;
		virtual std::shared_ptr<Texture> newTextureWithDMABuf(const TextureDescriptor& descriptor, int fd, uint64_t offset, uint64_t bytesPerRow) override;
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

		/**
		 * Stand-ins for resources a function uses but the caller never bound. Reading an unbound
		 * buffer through address 0, or an unwritten descriptor, faults or hangs the GPU; Metal leaves
		 * the result of such a read undefined, so reading zeros or a default sampler is conforming.
		 *
		 * The fallback buffer is a zero-filled shared buffer of fallbackBufferSize bytes.
		 */
		static constexpr size_t fallbackBufferSize = 64 * 1024;
		std::shared_ptr<Buffer> fallbackBuffer();
		std::shared_ptr<SamplerState> fallbackSampler();

		/**
		 * Whether VK_EXT_robustness2's nullDescriptor is enabled, which makes VK_NULL_HANDLE a valid
		 * image view or vertex buffer that reads as zero.
		 */
		bool nullDescriptorEnabled() const { return _nullDescriptor; }

		/**
		 * Held around every submission to and presentation on `graphicsQueue()`.
		 */
		std::mutex& graphicsQueueMutex() { return _graphicsQueueMutex; }

		/**
		 * Held from allocating a command buffer from `oneshotCommandPool()` until freeing it.
		 */
		std::mutex& oneshotCommandPoolMutex() { return _oneshotCommandPoolMutex; }

		/**
		 * The memory type a resource with the given storage mode is allocated from, among those
		 * `memoryTypeBits` allows; SIZE_MAX if none fits.
		 */
		size_t findMemoryType(uint32_t memoryTypeBits, StorageMode storageMode) const;

		/**
		 * The Vulkan device and its queues. They are created by the first call to any of these or to
		 * a new...() method; until then the device only holds what its VkPhysicalDevice reports.
		 */
		VkDevice device() const { ensureLogicalDevice(); return _device; }
		VkQueue graphicsQueue() const { ensureLogicalDevice(); return _graphicsQueue; }
		VkQueue computeQueue() const { ensureLogicalDevice(); return _computeQueue; }
		VkQueue transferQueue() const { ensureLogicalDevice(); return _transferQueue; }
		VkCommandPool oneshotCommandPool() const { ensureLogicalDevice(); return _oneshotCommandPool; }

		/** Whether the Vulkan device has been created yet. */
		bool logicalDeviceCreated() const { return _logicalDeviceCreated.load(std::memory_order_acquire); }

		INDIUM_PROPERTY(VkPhysicalDevice, p, P,hysicalDevice) = VK_NULL_HANDLE;
		INDIUM_PROPERTY(VkPhysicalDeviceProperties, p, P,roperties);
		INDIUM_PROPERTY(std::optional<uint32_t>, g, G,raphicsQueueFamilyIndex);
		INDIUM_PROPERTY(std::optional<uint32_t>, c, C,omputeQueueFamilyIndex);
		INDIUM_PROPERTY(std::optional<uint32_t>, t, T,ransferQueueFamilyIndex);
		// in theory, the presentation queue *can* be different from the graphics queue,
		// but in practice, they're actually the same.
		// TODO: maybe implement this just in case?
		//INDIUM_PROPERTY(std::optional<uint32_t>, p, P,resentQueueFamilyIndex);
		//INDIUM_PROPERTY(VkQueue, p, P,resentQueue) = VK_NULL_HANDLE;

		INDIUM_PROPERTY(VkPhysicalDeviceMemoryProperties, m, M,emoryProperties);
		INDIUM_PROPERTY_READONLY(Feature, f, F,eatures);

	public:
		/**
		 * A render pass for `info`. Render passes with the same description (see RenderPassKey)
		 * share one VkRenderPass, which the device owns and destroys; then `deviceOwned` is set.
		 * Otherwise the caller owns the new render pass and must destroy it.
		 */
		VkRenderPass acquireRenderPass(const VkRenderPassCreateInfo& info, bool& deviceOwned);

		/**
		 * Where buffers and textures get their device memory from.
		 */
		MemoryPool& memoryPool() { return _memoryPool; }

	private:
		// Kept after every other member: QuartzCore uses this class through pointers only, so
		// appending members does not move the ones it reads.
		static constexpr size_t maxCachedRenderPasses = 256;
		std::mutex _renderPassMutex;
		std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKey::Hash> _renderPasses;
		MemoryPool _memoryPool;
	};
};
