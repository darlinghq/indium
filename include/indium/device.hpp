#pragma once

#include <array>
#include <string>
#include <memory>
#include <cstdint>
#include <optional>
#include <vector>

#include <indium/init.hpp>
#include <indium/base.hpp>
#include <indium/types.hpp>
#include <indium/library.hpp>

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
		 * Whether textures of `PixelFormat::Depth24Unorm_Stencil8` can be created and rendered to.
		 */
		virtual bool supportsDepth24Stencil8PixelFormat() const = 0;

		/**
		 * Whether color and depth render targets with `sampleCount` samples per pixel can be created.
		 */
		virtual bool supportsTextureSampleCount(uint32_t sampleCount) const = 0;

		virtual std::shared_ptr<CommandQueue> newCommandQueue() = 0;
		virtual std::shared_ptr<RenderPipelineState> newRenderPipelineState(const RenderPipelineDescriptor& descriptor) = 0;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(const ComputePipelineDescriptor& descriptor, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) = 0;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(std::shared_ptr<Function> computeFunction, PipelineOption options = PipelineOption::None, std::shared_ptr<ComputePipelineReflection> reflection = nullptr) = 0;
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options) = 0;
		virtual std::shared_ptr<Buffer> newBuffer(const void* pointer, size_t length, ResourceOptions options) = 0;
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
