#pragma once

#include <array>
#include <string>
#include <memory>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <indium/init.hpp>
#include <indium/base.hpp>
#include <indium/types.hpp>
#include <indium/library.hpp>
#include <indium/event.hpp>
#include <indium/heap.hpp>
#include <indium/counter.hpp>

namespace Indium {
	class CommandQueue;
	class RenderPipelineState;
	struct RenderPipelineDescriptor;
	class Buffer;
	class Library;
	class Texture;
	struct TextureDescriptor;
	class SamplerState;
	struct SamplerDescriptor;
	class DepthStencilState;
	struct DepthStencilDescriptor;
	class ComputePipelineState;
	struct ComputePipelineDescriptor;
	class ComputePipelineReflection;
	class Function;

	enum class DeviceType {
		Other,
		IntegratedGPU,
		DiscreteGPU,
		VirtualGPU,
		CPU,
	};

	struct MemoryHeap {
		uint64_t size;
		/** Whether the heap is memory local to the device (e.g. video memory). */
		bool deviceLocal;
	};

	/**
	 * Counts of what a device has allocated and made since it was created. Always kept (they are plain
	 * counters); only reporting them is opt-in.
	 */
	struct DeviceStatistics {
		/** The bytes of live buffers, textures and heaps (see Device::currentAllocatedSize()). */
		uint64_t allocatedBytes;
		/** The largest value `allocatedBytes` has had. */
		uint64_t peakAllocatedBytes;
		/** Live device memory allocations made for buffers, textures and heaps. */
		uint64_t liveAllocations;
		/** Device memory allocations made, live or not. */
		uint64_t totalAllocations;
		/** Bytes of all allocations made, live or not. */
		uint64_t totalAllocatedBytes;
		/** Live Vulkan memory objects (pooled blocks and dedicated allocations). */
		uint64_t liveMemoryObjects;
		/** Vulkan memory objects made, live or not. */
		uint64_t totalMemoryObjects;
		/** Live heaps and the bytes they hold. */
		uint64_t liveHeaps;
		uint64_t liveHeapBytes;
		/** Heaps made, live or not. */
		uint64_t totalHeaps;
	};

	/** The GPU time spent in the draws or dispatches of one pipeline, while pipeline timing was enabled. */
	struct PipelineTiming {
		/**
		 * "compute <kernel>" for a compute pipeline, "render <vertex function> <fragment function>" for a render
		 * pipeline (the fragment function is empty when there is none).
		 */
		std::string name;
		/** The summed GPU time, in nanoseconds. */
		uint64_t gpuNanoseconds;
		/** The number of timed spans: dispatches for compute, runs of draws between pipeline changes for render. */
		uint64_t spans;
	};

	class Device {
	public:
		virtual ~Device() = 0;

		virtual std::string name() const = 0;

		/** The PCI vendor ID (or Khronos vendor ID) the Vulkan driver reports. */
		virtual uint32_t vendorID() const = 0;

		/** The vendor-specific device ID the Vulkan driver reports. */
		virtual uint32_t deviceID() const = 0;

		virtual DeviceType deviceType() const = 0;

		/** The memory heaps of the Vulkan device, in the driver's order. */
		virtual std::vector<MemoryHeap> memoryHeaps() const = 0;

		/**
		 * The Vulkan device UUID (VkPhysicalDeviceIDProperties::deviceUUID). It identifies the same
		 * physical device across processes and APIs, e.g. to match an OpenGL device to this one.
		 */
		virtual std::array<uint8_t, 16> deviceUUID() const = 0;

		/**
		 * The index of the Vulkan memory type Indium allocates resources of `storageMode` from,
		 * among the types `memoryTypeBits` allows (e.g. from VkMemoryRequirements). Lets code that
		 * imports memory into this device's Vulkan device choose the same way Indium does.
		 *
		 * @returns `std::nullopt` if no allowed type fits.
		 */
		virtual std::optional<uint32_t> memoryTypeIndex(uint32_t memoryTypeBits, StorageMode storageMode) const = 0;

		/**
		 * An approximation of how much memory this device can use with good
		 * performance, in bytes. Keeping the total size of all resources and
		 * heaps below this threshold avoids overcommitting the device and the
		 * performance penalty that comes with it.
		 *
		 * This is a soft limit, not a hard one: exceeding it is allowed and
		 * merely costs performance. It is also a point-in-time figure. A driver
		 * that reports a memory budget will see it move as the rest of the
		 * system allocates, so two calls need not agree.
		 */
		virtual uint64_t recommendedMaxWorkingSetSize() const = 0;

		/**
		 * The largest number of bytes a single buffer can be created with.
		 *
		 * Unlike recommendedMaxWorkingSetSize() this is a hard limit.
		 */
		virtual uint64_t maxBufferLength() const = 0;

		/**
		 * The most threads a threadgroup can have along each dimension. Each is capped by Vulkan's
		 * maximum invocations per workgroup and by Metal's 1024; the compute pipeline may limit the
		 * total further.
		 */
		virtual Size maxThreadsPerThreadgroup() const = 0;

		/**
		 * The most bytes of threadgroup memory a compute function can use, in total
		 * (VkPhysicalDeviceLimits::maxComputeSharedMemorySize).
		 */
		virtual size_t maxThreadgroupMemoryLength() const = 0;

		/**
		 * Whether textures of `PixelFormat::Depth24Unorm_Stencil8` can be created and rendered to.
		 */
		virtual bool supportsDepth24Stencil8PixelFormat() const = 0;

		/**
		 * Whether textures of `format` can be sampled in shaders, and, with `filtered`, sampled with
		 * linear filtering. False for a format the device cannot create textures of.
		 */
		virtual bool supportsPixelFormatSampling(PixelFormat format, bool filtered) const = 0;

		/**
		 * The row pitch alignment, in bytes, of a texture of `format` that is backed by a buffer
		 * (Buffer::newTexture()): the `bytesPerRow` of such a texture is a multiple of it. 0 for a format
		 * that cannot be backed by a buffer (compressed and depth/stencil formats).
		 */
		virtual size_t minimumLinearTextureAlignment(PixelFormat format) const = 0;

		/**
		 * Whether color and depth render targets with `sampleCount` samples per pixel can be created.
		 */
		virtual bool supportsTextureSampleCount(uint32_t sampleCount) const = 0;

		/**
		 * The bytes of device memory currently allocated for this device's buffers, textures and heaps. A resource
		 * made from a heap adds nothing (the heap's memory is counted whole), a texture view adds nothing, and the
		 * value goes down when a resource or heap is released. Memory that the caller supplied (a no-copy buffer)
		 * and memory the library keeps for itself is not counted. Safe to call from any thread.
		 */
		virtual uint64_t currentAllocatedSize() const = 0;

		virtual DeviceStatistics statistics() const = 0;

		/**
		 * Whether pipeline timing is possible: the device has hostQueryReset and timestamps on the graphics queue.
		 */
		virtual bool supportsPipelineTiming() const = 0;

		/**
		 * Turns GPU timing of the pipelines on or off (off by default). Command buffers encoded while it is on write
		 * Vulkan timestamps around the dispatches and the runs of draws of each pipeline, and add the elapsed
		 * time to `pipelineTimings()` when they have completed. Does nothing where `supportsPipelineTiming()` is false.
		 */
		virtual void setPipelineTimingEnabled(bool enabled) = 0;
		virtual bool pipelineTimingEnabled() const = 0;

		/** The time accumulated so far for each pipeline, ordered by name. */
		virtual std::vector<PipelineTiming> pipelineTimings() const = 0;

		virtual std::shared_ptr<CommandQueue> newCommandQueue() = 0;
		virtual std::shared_ptr<RenderPipelineState> newRenderPipelineState(const RenderPipelineDescriptor& descriptor) = 0;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(const ComputePipelineDescriptor& descriptor, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) = 0;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(std::shared_ptr<Function> computeFunction, PipelineOption options = PipelineOption::None, std::shared_ptr<ComputePipelineReflection> reflection = nullptr) = 0;
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options) = 0;
		virtual std::shared_ptr<Buffer> newBuffer(const void* pointer, size_t length, ResourceOptions options) = 0;

		/**
		 * Whether newBufferNoCopy() can import the caller's memory on this device (VK_EXT_external_memory_host is
		 * available). Without it newBufferNoCopy() still works, with a copy.
		 */
		virtual bool supportsBufferNoCopy() const = 0;

		/**
		 * A buffer whose storage is the memory at `pointer`, with no copy: what the CPU writes there is what the GPU
		 * reads and the other way round, and `contents()` is `pointer`. `options` must have the shared or the
		 * managed storage mode (the memory is coherent in both, so a managed buffer needs no `didModifyRange()`).
		 *
		 * The memory has to stay valid for the life of the buffer, which is not released before the GPU has finished
		 * with every command buffer that uses it; `deallocator` (which may be empty) is called with `pointer` and
		 * `length` after that, on whichever thread releases the buffer last. It is not called when this returns
		 * nullptr.
		 *
		 * `pointer` and `length` need not be aligned. The device is given the pages the range lies in, so the
		 * buffer can reach the rest of the first and last page, the way a GPU access reaches past the end of any
		 * host mapping. Two buffers may overlap.
		 *
		 * When the memory cannot be imported (no VK_EXT_external_memory_host, the driver refuses it, or the device
		 * wants more than a page of alignment) the buffer has memory of its own instead, a warning is printed to
		 * stderr the first time that happens in a process, and `contents()` is still `pointer`: the bytes there are
		 * copied to the buffer when a command buffer is committed and copied back when a command buffer completes.
		 * CPU access to the memory while the GPU may use the buffer is therefore not seen as it would be with an
		 * import.
		 *
		 * @returns nullptr for a null pointer, a zero length or another storage mode.
		 */
		virtual std::shared_ptr<Buffer> newBufferNoCopy(void* pointer, size_t length, ResourceOptions options, std::function<void(void*, size_t)> deallocator) = 0;
		/**
		 * Translates Metal Shading Language source and creates a library from
		 * it.
		 *
		 * @returns `nullptr` if the library could not be translated.
		 */
		virtual std::shared_ptr<Library> newLibrary(const void* data, size_t length) = 0;
		/**
		 * Creates a library from a module some other producer already compiled
		 * to SPIR-V. The module must target Vulkan and must use the conventions
		 * indium binds resources with: buffer addresses arriving through one
		 * uniform buffer per stage at descriptor set 0, binding 0, and the
		 * image and sampler bindings named by `reflection`.
		 *
		 * Neither argument is owned. `spirv` is read only for the duration of
		 * this call, because `vkCreateShaderModule` copies the code it is
		 * given, and `reflection` is copied into the library. The caller may
		 * free, reuse or destroy both as soon as this returns. This is the
		 * opposite of the source overload above, which receives a buffer that
		 * Iridium allocated and therefore frees.
		 *
		 * @returns `nullptr`, with a description of the problem in
		 *          `errorMessage` when it is not null and empty when it is not,
		 *          if:
		 *          - `spirv` is null, shorter than a SPIR-V header, not a whole
		 *            number of words, or does not start with the SPIR-V magic
		 *            number;
		 *          - `reflection` describes no function, describes a function
		 *            with no name or a stage indium cannot bind resources for,
		 *            or names a function whose name does not occur in `spirv`;
		 *          - two bindings of one function claim the same descriptor
		 *            binding number;
		 *          - a binding or an embedded sampler field holds a value
		 *            outside its enumeration.
		 *
		 * A `nullptr` return means nothing was created, so a caller never has
		 * to cope with a library whose bindings were dropped or defaulted.
		 *
		 * @note A name that occurs in the module but is not an entry point of
		 *       it is not detected here; indium finds that out when the
		 *       pipeline is created. Producing reflection keys that match the
		 *       module's entry points is the producer's responsibility.
		 */
		virtual std::shared_ptr<Library> newLibrary(const void* spirv, size_t length, const LibraryReflection& reflection, std::string* errorMessage = nullptr) = 0;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor) = 0;

		/**
		 * Allocates a heap's memory up front.
		 *
		 * @returns `nullptr` for a sparse, Managed or Memoryless heap, a size of 0, or when the memory cannot be allocated.
		 */
		virtual std::shared_ptr<Heap> newHeap(const HeapDescriptor& descriptor) = 0;

		/**
		 * What a buffer of `length` bytes takes in a heap with the storage mode of `options` (this device's own
		 * requirement for it): its size and the alignment its offset must have. The size is 0 for a length of 0.
		 */
		virtual SizeAndAlign heapBufferSizeAndAlign(size_t length, ResourceOptions options) = 0;

		/**
		 * What a texture takes in a heap, like `heapBufferSizeAndAlign()`; {0, 0} if the device cannot make the texture.
		 */
		virtual SizeAndAlign heapTextureSizeAndAlign(const TextureDescriptor& descriptor) = 0;

		/**
		 * Whether this device can import Linux DMA-BUFs as textures (VK_EXT_external_memory_dma_buf
		 * and VK_EXT_image_drm_format_modifier are available).
		 */
		virtual bool supportsDMABufImport() const = 0;

		/**
		 * Creates a single-level 2D texture whose storage is a linear image in a Linux DMA-BUF: the
		 * first row starts `offset` bytes into the buffer and rows are `bytesPerRow` bytes apart.
		 * Rendering into the texture and CPU access to the buffer see the same memory.
		 *
		 * The descriptor `fd` is duplicated; the caller keeps its own. Returns `nullptr` if the device
		 * cannot import the buffer with this format, size, usage and layout.
		 */
		virtual std::shared_ptr<Texture> newTextureWithDMABuf(const TextureDescriptor& descriptor, int fd, uint64_t offset, uint64_t bytesPerRow) = 0;
		virtual std::shared_ptr<SamplerState> newSamplerState(const SamplerDescriptor& descriptor) = 0;
		virtual std::shared_ptr<DepthStencilState> newDepthStencilState(const DepthStencilDescriptor& descriptor) = 0;

		/**
		 * An event, a counter for ordering command buffers (`CommandBuffer::encodeSignalEvent()` and
		 * `encodeWaitForEvent()`).
		 */
		virtual std::shared_ptr<Event> newEvent() = 0;
		virtual std::shared_ptr<SharedEvent> newSharedEvent() = 0;

		/**
		 * The event `handle` names, as an object of its own that shares the value, or `nullptr` if the handle is
		 * empty or the event belongs to another device.
		 */
		virtual std::shared_ptr<SharedEvent> newSharedEvent(const std::shared_ptr<SharedEventHandle>& handle) = 0;
		virtual std::shared_ptr<Fence> newFence() = 0;

		// --- counters ---

		/**
		 * The counter sets this device can sample. Currently only the timestamp set, when the
		 * device can write timestamps from its graphics queue and reset a query pool from the host.
		 */
		virtual std::vector<CounterSet> counterSets() const = 0;

		/**
		 * Whether a sample can be taken at `point`. Stage boundaries (the start and end of a pass),
		 * dispatch boundaries (between compute commands) and blit boundaries (between blit commands)
		 * are supported when timestamps are; draw and tile dispatch boundaries are not.
		 */
		virtual bool supportsCounterSampling(CounterSamplingPoint point) const = 0;

		/**
		 * @return the new buffer, or null with `failure` describing why (if `failure` is non-null).
		 */
		virtual std::shared_ptr<CounterSampleBuffer> newCounterSampleBuffer(const CounterSampleBufferDescriptor& descriptor, CounterSampleBufferFailure* failure = nullptr) = 0;

		/**
		 * Samples the host's monotonic clock and the device's timestamp clock as close together as
		 * the driver allows, both in nanoseconds. Where the driver cannot correlate the clocks, the
		 * device value is taken from the host clock.
		 */
		virtual void sampleTimestamps(uint64_t* cpuTimestamp, uint64_t* gpuTimestamp) = 0;

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

	/**
	 * Like `createSystemDefaultDevice()`, but returns the device `preferredDevice` names if there is
	 * one: either the decimal index of a device in `copyAllDevices()` or a part of a device's name,
	 * compared without regard to case. An empty or unmatched preference selects the default device.
	 */
	std::shared_ptr<Device> createSystemDefaultDevice(const std::string& preferredDevice);

	/**
	 * All devices Indium can use, the default one first. GPUs come before other devices, discrete GPUs
	 * first; a CPU rasterizer is only listed when there is no GPU.
	 */
	std::vector<std::shared_ptr<Device>> copyAllDevices();
};
