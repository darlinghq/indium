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
#include <indium/texture-upload.private.hpp>
#include <indium/pipeline-cache.private.hpp>
#include <indium/pipeline-timing.private.hpp>

namespace Indium {
	class PrivateDevice;
	class PrivateBuffer;

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

		std::mutex _shadowMutex;
		std::vector<PrivateBuffer*> _shadowBuffers;
		std::atomic<bool> _bufferNoCopyImportDisabled { false };
		std::mutex _fallbackMutex;
		std::shared_ptr<Buffer> _fallbackBuffer;
		std::shared_ptr<SamplerState> _fallbackSampler;
		bool _nullDescriptor = false;
		bool _fillModeNonSolid = false;
		bool _depthClamp = false;
		bool _occlusionQueryPrecise = false;
		bool _hostQueryReset = false;
		uint32_t _timestampValidBits = 0;
		bool _calibratedTimestamps = false;
		std::atomic<bool> _pipelineTiming { false };
		PipelineTimingTable _pipelineTimingTable;
		std::atomic<uint64_t> _liveHeaps { 0 };
		std::atomic<uint64_t> _liveHeapBytes { 0 };
		std::atomic<uint64_t> _totalHeaps { 0 };
		std::mutex _visibilityResolveMutex;
		std::shared_ptr<ComputePipelineState> _visibilityResolvePipeline;
		std::atomic<bool> _bufferShadowTextures { false };
		bool _nativeA8Unorm = false;

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
			Maintenance5        = 1 << 7,
			ExternalMemoryHost  = 1 << 8,
			ShaderAtomicFloat   = 1 << 9,
			PresentWait         = 1 << 10,
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
		virtual Size maxThreadsPerThreadgroup() const override;
		virtual size_t maxThreadgroupMemoryLength() const override;
		virtual uint32_t vendorID() const override;
		virtual uint32_t deviceID() const override;
		virtual DeviceType deviceType() const override;
		virtual std::vector<MemoryHeap> memoryHeaps() const override;
		virtual uint64_t currentAllocatedSize() const override;
		virtual DeviceStatistics statistics() const override;
		virtual bool supportsPipelineTiming() const override;
		virtual void setPipelineTimingEnabled(bool enabled) override;
		virtual bool pipelineTimingEnabled() const override { return _pipelineTiming.load(std::memory_order_relaxed); }
		virtual std::vector<PipelineTiming> pipelineTimings() const override { return _pipelineTimingTable.snapshot(); }

		PipelineTimingTable& pipelineTimingTable() { return _pipelineTimingTable; }
		/** Nanoseconds per timestamp tick, and the bits of a timestamp the graphics queue keeps. */
		double timestampPeriod() const { return _properties.limits.timestampPeriod; }
		uint32_t timestampValidBits() const { return _timestampValidBits; }

		/** Heaps are counted here (see DeviceStatistics); called by PrivateHeap. */
		void noteHeapCreated(uint64_t size);
		void noteHeapDestroyed(uint64_t size);
		virtual std::array<uint8_t, 16> deviceUUID() const override;
		virtual std::optional<uint32_t> memoryTypeIndex(uint32_t memoryTypeBits, StorageMode storageMode) const override;
		virtual bool supportsDepth24Stencil8PixelFormat() const override;
		virtual bool supportsPixelFormatSampling(PixelFormat format, bool filtered) const override;
		virtual size_t minimumLinearTextureAlignment(PixelFormat format) const override;
		virtual bool supportsTextureSampleCount(uint32_t sampleCount) const override;
		virtual std::shared_ptr<CommandQueue> newCommandQueue() override;
		virtual std::shared_ptr<RenderPipelineState> newRenderPipelineState(const RenderPipelineDescriptor& descriptor) override;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(const ComputePipelineDescriptor& descriptor, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) override;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(std::shared_ptr<Function> computeFunction, PipelineOption options = PipelineOption::None, std::shared_ptr<ComputePipelineReflection> reflection = nullptr) override;
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options) override;
		virtual std::shared_ptr<Buffer> newBuffer(const void* pointer, size_t length, ResourceOptions options) override;
		virtual bool supportsBufferNoCopy() const override;
		virtual std::shared_ptr<Buffer> newBufferNoCopy(void* pointer, size_t length, ResourceOptions options, std::function<void(void*, size_t)> deallocator) override;
		/**
		 * The copying no-copy buffers (see PrivateBuffer::NoCopyShadow) are kept in step with the caller's memory:
		 * `toDevice` copies the caller's memory to every one of them, otherwise they are copied back.
		 */
		void syncShadowBuffers(bool toDevice);
		void addShadowBuffer(PrivateBuffer* buffer);
		void removeShadowBuffer(PrivateBuffer* buffer);
		/** For tests: when set, newBufferNoCopy() acts as if the device could not import host memory. */
		void setBufferNoCopyImportDisabled(bool disabled) { _bufferNoCopyImportDisabled = disabled; }
		virtual std::shared_ptr<Library> newLibrary(const void* data, size_t length) override;
		virtual std::shared_ptr<Library> newLibrary(const void* spirv, size_t length, const LibraryReflection& reflection, std::string* errorMessage = nullptr) override;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor) override;
		virtual std::shared_ptr<Heap> newHeap(const HeapDescriptor& descriptor) override;
		virtual SizeAndAlign heapBufferSizeAndAlign(size_t length, ResourceOptions options) override;
		virtual SizeAndAlign heapTextureSizeAndAlign(const TextureDescriptor& descriptor) override;
		virtual bool supportsDMABufImport() const override;
		virtual std::shared_ptr<Texture> newTextureWithDMABuf(const TextureDescriptor& descriptor, int fd, uint64_t offset, uint64_t bytesPerRow) override;
		virtual std::shared_ptr<SamplerState> newSamplerState(const SamplerDescriptor& descriptor) override;
		virtual std::shared_ptr<DepthStencilState> newDepthStencilState(const DepthStencilDescriptor& descriptor) override;
		virtual std::shared_ptr<Event> newEvent() override;
		virtual std::shared_ptr<SharedEvent> newSharedEvent() override;
		virtual std::shared_ptr<SharedEvent> newSharedEvent(const std::shared_ptr<SharedEventHandle>& handle) override;
		virtual std::shared_ptr<Fence> newFence() override;
		virtual std::vector<CounterSet> counterSets() const override;
		virtual bool supportsCounterSampling(CounterSamplingPoint point) const override;
		virtual std::shared_ptr<CounterSampleBuffer> newCounterSampleBuffer(const CounterSampleBufferDescriptor& descriptor, CounterSampleBufferFailure* failure) override;
		virtual void sampleTimestamps(uint64_t* cpuTimestamp, uint64_t* gpuTimestamp) override;

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
		 * Whether the device can rasterize triangles as lines (Vulkan's fillModeNonSolid, which is enabled
		 * when the device has it).
		 */
		bool fillModeNonSolidEnabled() const { return _fillModeNonSolid; }

		/**
		 * Whether the device can clamp fragment depth to the viewport range instead of clipping it (Vulkan's
		 * depthClamp, which is enabled when the device has it).
		 */
		bool depthClampEnabled() const { return _depthClamp; }

		/**
		 * Whether occlusion queries can count samples (Vulkan's occlusionQueryPrecise, enabled when the device has
		 * it). Without it a query only tells whether any sample passed.
		 */
		bool occlusionQueryPreciseEnabled() const { return _occlusionQueryPrecise; }

		/**
		 * Whether a query pool can be reset from the host (Vulkan's hostQueryReset, enabled when the device has
		 * it). Visibility results need it: the number of queries a render pass uses is only known as it is
		 * encoded, and a query pool cannot be reset inside a render pass.
		 */
		bool hostQueryResetEnabled() const { return _hostQueryReset; }

		/**
		 * The compute pipeline that folds the occlusion query results of a render pass into a visibility result
		 * buffer (see VisibilityResults), made on first use.
		 */
		std::shared_ptr<ComputePipelineState> visibilityResolvePipeline();

		/**
		 * Drops the objects the device keeps for its own use (they hold a reference to the device): the visibility
		 * pipeline, and the uploads in flight and staging buffers of the TextureUploader, after waiting for the GPU
		 * to finish with them. Called when the device list is torn down.
		 */
		void releaseCachedObjects();

		/**
		 * Whether a texture was ever made that is an image of its own kept equal to a buffer's rows (see
		 * ConcreteTexture::BufferBacking). Command buffers only split into segments, so that the copies can be
		 * recorded around the encoders, once there is one.
		 */
		void noteBufferShadowTexture() { _bufferShadowTextures.store(true, std::memory_order_relaxed); }
		bool hasBufferShadowTextures() const { return _bufferShadowTextures.load(std::memory_order_relaxed); }

		/**
		 * Whether A8Unorm textures use VK_FORMAT_A8_UNORM_KHR, which a shader can sample, write and render to
		 * like any other format. Without it (no VK_KHR_maintenance5, or the format lacks a feature) A8Unorm is
		 * R8 read through a swizzle, which only supports reads.
		 */
		bool nativeA8UnormEnabled() const { return _nativeA8Unorm; }

		/** The Vulkan format a texture of `pixelFormat` has on this device. */
		VkFormat textureVkFormat(PixelFormat pixelFormat) const {
			if (pixelFormat == PixelFormat::A8Unorm && _nativeA8Unorm) {
				return VK_FORMAT_A8_UNORM_KHR;
			}
			return pixelFormatToVkFormat(pixelFormat);
		};

		/** What a shader sees of the stored channels of `pixelFormat` on this device. */
		bool needsImplicitSwizzle(PixelFormat pixelFormat) const {
			return pixelFormatNeedsImplicitSwizzle(pixelFormat) && !_nativeA8Unorm;
		};

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

		/**
		 * The host writes to textures that are waiting for the GPU (see TextureUploader).
		 */
		TextureUploader& textureUploader() { return _textureUploader; }

		/**
		 * The pipeline cache every Vulkan pipeline of this device is created with. It is persisted in the
		 * user's cache directory (see PipelineCache), or kept in memory if that is not possible.
		 */
		PipelineCache& pipelineCache() { ensureLogicalDevice(); return *_pipelineCache; }

		/**
		 * Creates one pipeline through the device's cache and records the change for the next save.
		 */
		VkResult createGraphicsPipeline(const VkGraphicsPipelineCreateInfo& info, VkPipeline* pipeline);
		VkResult createComputePipeline(const VkComputePipelineCreateInfo& info, VkPipeline* pipeline);

	private:
		// Kept after every other member: QuartzCore uses this class through pointers only, so
		// appending members does not move the ones it reads.
		static constexpr size_t maxCachedRenderPasses = 256;
		std::mutex _renderPassMutex;
		std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKey::Hash> _renderPasses;
		MemoryPool _memoryPool;
		TextureUploader _textureUploader { *this };
		std::unique_ptr<PipelineCache> _pipelineCache;
	};
};
