#pragma once

#include <vulkan/vulkan.h>

#include <indium/texture.hpp>
#include <indium/types.private.hpp>
#include <indium/memory-pool.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/heap.private.hpp>
#include <indium/hazard-tracker.private.hpp>

#include <mutex>
#include <optional>
#include <unordered_map>

namespace Indium {
	class PrivateCommandBuffer;
	class ConcreteTexture;

	/**
	 * An abstract base class for Indium textures.
	 *
	 * All instances of classes derived from this class must ONLY be created as
	 * shared pointers.
	 */
	class PrivateTexture: public Texture, public std::enable_shared_from_this<PrivateTexture> {
	protected:
		std::mutex _syncMutex;
		std::shared_ptr<TimelineSemaphore> _syncSemaphore;
		std::shared_ptr<BinarySemaphore> _extraWaitSemaphore;

		std::mutex _presentationMutex;
		std::shared_ptr<BinarySemaphore> _presentationSemaphore;
		std::shared_ptr<PrivateDevice> _device;

	public:
		explicit PrivateTexture(std::shared_ptr<PrivateDevice> device);
		virtual ~PrivateTexture() = 0;

		/** The buffer whose memory holds the texels, which getBytes reads from the host, or none. */
		virtual std::shared_ptr<PrivateBuffer> hostBackingBuffer() { return nullptr; }

		virtual std::shared_ptr<Texture> newTextureView(PixelFormat pixelFormat) override;
		virtual std::shared_ptr<Texture> newTextureView(PixelFormat pixelFormat, TextureType textureType, const Range<size_t>& levels, const Range<size_t>& layers) override;
		virtual std::shared_ptr<Texture> newTextureView(PixelFormat pixelFormat, TextureType textureType, const Range<size_t>& levels, const Range<size_t>& layers, const TextureSwizzleChannels& swizzle) override;

		virtual std::shared_ptr<Indium::Device> device() override;

		/**
		 * Returns a pointer to a Vulkan image view that Indium can use internally.
		 *
		 * Implementations are required to return a valid image view.
		 */
		virtual VkImageView imageView() = 0;

		/**
		 * Returns a pointer to a Vulkan image that Indium can use internally.
		 * This is used, for example, to create image views for alternative pixel or
		 * texture formats from that share the same backing image.
		 *
		 * Implementations are required to return a valid image.
		 */
		virtual VkImage image() = 0;

		virtual VkImageLayout imageLayout() = 0;

		/**
		 * The Vulkan buffer view of a texture_buffer texture (MTLTextureTypeTextureBuffer), which is bound as a
		 * texel buffer; VK_NULL_HANDLE for every other texture, which has an image instead.
		 */
		virtual VkBufferView bufferView() { return VK_NULL_HANDLE; }

		/**
		 * Where the image's bytes are, for hazard tracking. A view says the same as the texture it was made from, a
		 * texture made over a buffer's memory the same as that buffer, so that they are one resource to the tracker.
		 */
		virtual HazardRange hazardRange() { return HazardRange::image(image()); }

		/**
		 * The Vulkan format the underlying image was created with. Views of other pixel formats are made
		 * from it, so it is what a new view's format is judged against. A texture that does not own its
		 * image says what it was created as.
		 */
		virtual VkFormat imageFormat() { return pixelFormatToVkFormat(pixelFormat()); }

		/**
		 * Whether views of the image may use a Vulkan format other than `imageFormat()`
		 * (VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT). Only an image that indium creates can say no: an image that
		 * comes from elsewhere, such as a swapchain image, is not second-guessed.
		 */
		virtual bool imageIsMutableFormat() { return true; }

		/**
		 * The view to bind in a descriptor. Descriptors may only name a single aspect, so for a
		 * combined depth/stencil texture this is a depth-only view; otherwise it is `imageView()`.
		 */
		virtual VkImageView sampledImageView() { return imageView(); }

		/**
		 * The view to bind for a TextureAccessType::Fetch binding: `sampledImageView()`, except that a cube
		 * or cube array is viewed as the 2D array of its faces, as an image fetch takes no cube image.
		 */
		virtual VkImageView fetchImageView() { return sampledImageView(); }

		virtual size_t vulkanArrayLength() const;

		// TODO: figure out how to allow simultaneous reads and properly synchronize them with writes.
		//       the problem is that we need to signal the timeline semaphore once the final read is done
		//       because timeline semaphores only allow the counter to increase, but we can't know which
		//       read is the final one until it actually executes. if timeline semaphores provided some
		//       an increment operation, everything would be fine, but unfortunately, they do not.
		//
		//       one possible solution involves messing around with semaphore wait/signal stage masks.
		//       it's a bit unclear whether specifying `VK_PIPELINE_STAGE_2_NONE` for the wait/src stage
		//       would cause the wait to occur before a signal with a `VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT` signal/dst stage.
		//       if so, then we can use two waits for reads/writes: the first wait is the actual wait for reading
		//       from the texture and would use the normal stage mask (depending on when the texture is read).
		//       the second wait would use the "none" wait stage and then the signal would use the "all commands" stage.
		//       this would ensure that the semaphore is always incremented and shouldn't block command execution.

		/**
		 * @param[out] waitValue A value to wait on with the semaphore before using the texture.
		 * @param[out] extraWaitSemaphore If an extra wait is required (e.g. for images acquired from a swapchain),
		 *                                the extra semaphore is returned through this reference. Otherwise, `nullptr`
		 *                                is written instead.
		 * @param[out] signalValue A value to signal the semaphore with once you're done. If the value returned is `0`,
		 *                         then you don't actually need to signal the semaphore when you're done.
		 */
		virtual const TimelineSemaphore& acquire(uint64_t& waitValue, std::shared_ptr<BinarySemaphore>& extraWaitSemaphore, uint64_t& signalValue);

		// TODO: optimize this better to avoid blocking callers of `synchronizePresentation` while someone is updating the presentation semaphore
		virtual void beginUpdatingPresentationSemaphore(std::shared_ptr<BinarySemaphore> presentationSemaphore);
		virtual void endUpdatingPresentationSemaphore();
		virtual std::shared_ptr<BinarySemaphore> synchronizePresentation();

		virtual void precommit(std::shared_ptr<Indium::PrivateCommandBuffer> cmdbuf);
		virtual bool needsExportablePresentationSemaphore() const;

		/**
		 * Whether the image is a copy of the rows of a buffer that is kept equal to the buffer by copies around the
		 * encoders that use it (BufferBacking::shadow). The command buffer records the copies, outside of any render pass.
		 */
		virtual bool isBufferShadow() { return false; }
		/** Records the copy of the buffer's rows into the image, and leaves the image in the general layout. */
		virtual void recordBufferToImage(VkCommandBuffer commandBuffer) {}
		/** Records the copy of the image into the buffer's rows, and makes it visible to later commands and the host. */
		virtual void recordImageToBuffer(VkCommandBuffer commandBuffer) {}
		/** Whether a shader may write the texture (it was made with ShaderWrite usage). */
		virtual bool mayBeWrittenByShader() { return false; }

		// Declared here so this class has a complete vtable. The readback that has an
		// image to copy from lives in ConcreteTexture and TextureView, which override
		// both; without these, every other subclass of PrivateTexture stays abstract.
		virtual void getBytes(Indium::Region region, size_t mipmapLevel, void* bytes, size_t bytesPerRow) override;
		virtual void getBytes(Indium::Region region, size_t mipmapLevel, size_t slice, void* bytes, size_t bytesPerRow, size_t bytesPerImage) override;
	};

	class TextureView: public PrivateTexture {
		INDIUM_PREVENT_COPY(TextureView);

	private:
		std::shared_ptr<PrivateTexture> _original;

	public:
		virtual std::shared_ptr<PrivateBuffer> hostBackingBuffer() override { return _original->hostBackingBuffer(); }

	private:
		VkImageView _imageView;
		VkImageView _sampledImageView = VK_NULL_HANDLE;
		VkImageView _faceArrayView = VK_NULL_HANDLE;
		PixelFormat _pixelFormat;
		TextureType _textureType;
		VkImageAspectFlags _imageAspect;
		Range<size_t> _levels;
		Range<size_t> _layers;
		TextureSwizzleChannels _swizzle;
		// where the view starts in the image of the texture it was made from (through any views in between)
		size_t _rootLevel = 0;
		size_t _rootLayer = 0;

		std::shared_ptr<ConcreteTexture> concreteRoot() const;
		size_t levelExtent(size_t extent) const;

	public:
		TextureView(std::shared_ptr<PrivateTexture> original, PixelFormat pixelFormat, TextureType textureType, VkImageAspectFlags imageAspect, TextureSwizzleChannels swizzle, const Range<size_t>& levels, const Range<size_t>& layers);
		~TextureView();

		virtual TextureType textureType() const override;
		virtual PixelFormat pixelFormat() const override;
		virtual size_t width() const override;
		virtual size_t height() const override;
		virtual size_t depth() const override;
		virtual size_t mipmapLevelCount() const override;
		virtual size_t arrayLength() const override;
		virtual size_t sampleCount() const override;
		virtual bool framebufferOnly() const override;
		virtual TextureUsage usage() const override;
		virtual bool allowGPUOptimizedContents() const override;
		virtual bool shareable() const override;
		virtual TextureSwizzleChannels swizzle() const override;

		virtual std::shared_ptr<Texture> parentTexture() const override;
		virtual size_t parentRelativeLevel() const override;
		virtual size_t parentRelativeSlice() const override;

		virtual VkImageView imageView() override;
		virtual VkImageView sampledImageView() override;
		virtual VkImageView fetchImageView() override;
		virtual VkImage image() override;
		virtual std::shared_ptr<Device> device() override;
		virtual VkImageLayout imageLayout() override;
		virtual VkFormat imageFormat() override;
		virtual bool imageIsMutableFormat() override;
		virtual HazardRange hazardRange() override { return _original->hazardRange(); }
		virtual HazardTrackingMode hazardTrackingMode() const override { return _original->hazardTrackingMode(); }

		// a view is part of its texture's memory
		virtual std::shared_ptr<Heap> heap() override;
		virtual size_t heapOffset() override;
		virtual size_t allocatedSize() override;
		virtual void makeAliasable() override;
		virtual bool isAliasable() override;

		virtual const TimelineSemaphore& acquire(uint64_t& waitValue, std::shared_ptr<BinarySemaphore>& extraWaitSemaphore, uint64_t& signalValue) override;
		virtual void beginUpdatingPresentationSemaphore(std::shared_ptr<BinarySemaphore> presentationSemaphore) override;
		virtual void endUpdatingPresentationSemaphore() override;
		virtual std::shared_ptr<BinarySemaphore> synchronizePresentation() override;

		virtual bool isBufferShadow() override;
		virtual void recordBufferToImage(VkCommandBuffer commandBuffer) override;
		virtual void recordImageToBuffer(VkCommandBuffer commandBuffer) override;
		virtual bool mayBeWrittenByShader() override;

		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, const void* bytes, size_t bytesPerRow) override;
		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, size_t slice, const void* bytes, size_t bytesPerRow, size_t bytesPerImage) override;
		virtual void getBytes(Indium::Region region, size_t mipmapLevel, void* bytes, size_t bytesPerRow) override;
		virtual void getBytes(Indium::Region region, size_t mipmapLevel, size_t slice, void* bytes, size_t bytesPerRow, size_t bytesPerImage) override;

		virtual void precommit(std::shared_ptr<Indium::PrivateCommandBuffer> cmdbuf) override;
		virtual bool needsExportablePresentationSemaphore() const override;
	};

	/**
	 * A texture of type TextureBuffer: a range of a buffer that shaders read and write as one-dimensional texels
	 * (the texture_buffer class). It has no image of its own: the buffer holds the texels, and a buffer view
	 * describes the format and range. Texels are not accessed from the host through the texture.
	 */
	class BufferTexture: public PrivateTexture {
		INDIUM_PREVENT_COPY(BufferTexture);

	private:
		TextureDescriptor _descriptor;
		std::shared_ptr<PrivateBuffer> _buffer;
		size_t _offset;
		VkBufferView _view = VK_NULL_HANDLE;

	public:
		/** Throws std::runtime_error if the format cannot be a texel buffer on the device or the range is outside the buffer. */
		BufferTexture(std::shared_ptr<PrivateDevice> device, const TextureDescriptor& descriptor, std::shared_ptr<PrivateBuffer> buffer, size_t offset);
		virtual ~BufferTexture();

		virtual TextureType textureType() const override;
		virtual PixelFormat pixelFormat() const override;
		virtual size_t width() const override;
		virtual size_t height() const override;
		virtual size_t depth() const override;
		virtual size_t mipmapLevelCount() const override;
		virtual size_t arrayLength() const override;
		virtual size_t sampleCount() const override;
		virtual bool framebufferOnly() const override;
		virtual TextureUsage usage() const override;
		virtual bool allowGPUOptimizedContents() const override;
		virtual bool shareable() const override;
		virtual TextureSwizzleChannels swizzle() const override;

		virtual VkImageView imageView() override;
		virtual VkImage image() override;
		virtual VkImageLayout imageLayout() override;
		virtual VkBufferView bufferView() override;
		virtual HazardRange hazardRange() override;
		virtual HazardTrackingMode hazardTrackingMode() const override;
		virtual bool mayBeWrittenByShader() override;

		virtual std::shared_ptr<Buffer> buffer() const override;
		virtual size_t bufferOffset() const override;
		virtual size_t bufferBytesPerRow() const override;

		virtual std::shared_ptr<Texture> newTextureView(PixelFormat pixelFormat, TextureType textureType, const Range<size_t>& levels, const Range<size_t>& layers) override;
		virtual std::shared_ptr<Texture> newTextureView(PixelFormat pixelFormat, TextureType textureType, const Range<size_t>& levels, const Range<size_t>& layers, const TextureSwizzleChannels& swizzle) override;

		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, const void* bytes, size_t bytesPerRow) override;
		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, size_t slice, const void* bytes, size_t bytesPerRow, size_t bytesPerImage) override;
	};

	class ConcreteTexture: public PrivateTexture {
		INDIUM_PREVENT_COPY(ConcreteTexture);

	private:
		TextureDescriptor _descriptor;
		VkImage _image = VK_NULL_HANDLE;
		VkImageView _imageView = VK_NULL_HANDLE;
		VkImageView _sampledImageView = VK_NULL_HANDLE;
		VkImageView _faceArrayView = VK_NULL_HANDLE;
		VkDeviceMemory _memory = VK_NULL_HANDLE;
		StorageMode _storageMode;
		MemoryPool::Allocation _allocation;
		std::shared_ptr<PrivateBuffer> _backingBuffer;

	public:
		virtual std::shared_ptr<PrivateBuffer> hostBackingBuffer() override { return _backingBuffer; }

	private:
		size_t _backingOffset = 0;
		size_t _backingBytesPerRow = 0;
		bool _mutableFormat = false;
		bool _shadow = false;
		HazardTrackingMode _hazardTrackingMode = HazardTrackingMode::HazardTrackingModeTracked;
		HazardRange _hazardRange;
		VkDeviceSize _requiredSize = 0;
		// set for a texture made from a heap, which owns the memory
		std::unique_ptr<HeapAllocation> _heapAllocation;

	public:
		// A linear image in a Linux DMA-BUF to use as the texture's storage.
		struct DMABufImport {
			int fd;
			uint64_t offset;
			uint64_t bytesPerRow;
		};

		// The rows of a buffer as the pixels of a texture (see Buffer::newTexture()).
		// With `shadow`, the texture is an image of its own that is kept equal to the buffer by copying around the
		// encoders that use it (recordBufferToImage() before, recordImageToBuffer() after a write), which is how a
		// row pitch the device's linear images cannot have is supported.
		struct BufferBacking {
			std::shared_ptr<PrivateBuffer> buffer;
			size_t offset;
			size_t bytesPerRow;
			bool shadow = false;
		};

		// The memory of a heap, at `offset` for a placement heap (HeapAllocation).
		//
		// With `query` set (and no heap), nothing is created: the image is only made to ask the driver what it
		// would need, which is written to `*query`; the object is then not usable (Device::heapTextureSizeAndAlign()).
		struct HeapBacking {
			std::shared_ptr<PrivateHeap> heap;
			std::optional<size_t> offset;
			VkMemoryRequirements* query = nullptr;
		};

		// With `import`, the texture's storage is imported instead of allocated, with `backing` it is the
		// memory of a buffer, and with `heapBacking` it is a range of a heap's memory; every failure to create
		// it throws std::runtime_error.
		ConcreteTexture(std::shared_ptr<PrivateDevice> device, const TextureDescriptor& descriptor, const DMABufImport* import = nullptr, const BufferBacking* backing = nullptr, const HeapBacking* heapBacking = nullptr);
		virtual ~ConcreteTexture();

	private:
		bool importMemory(const DMABufImport& import, const VkMemoryRequirements& reqs);

	public:

		virtual TextureType textureType() const override;
		virtual PixelFormat pixelFormat() const override;
		virtual size_t width() const override;
		virtual size_t height() const override;
		virtual size_t depth() const override;
		virtual size_t mipmapLevelCount() const override;
		virtual size_t arrayLength() const override;
		virtual size_t sampleCount() const override;
		virtual bool framebufferOnly() const override;
		virtual TextureUsage usage() const override;
		virtual bool allowGPUOptimizedContents() const override;
		virtual bool shareable() const override;
		virtual TextureSwizzleChannels swizzle() const override;

		virtual VkImageView imageView() override;
		virtual VkImageView sampledImageView() override;
		virtual VkImageView fetchImageView() override;
		virtual VkImage image() override;
		virtual VkImageLayout imageLayout() override;
		virtual VkFormat imageFormat() override;
		virtual bool imageIsMutableFormat() override;
		virtual size_t vulkanArrayLength() const override;
		virtual HazardRange hazardRange() override { return _hazardRange.valid() ? _hazardRange : HazardRange::image(_image); }
		virtual HazardTrackingMode hazardTrackingMode() const override { return _hazardTrackingMode; }

		virtual bool isBufferShadow() override;
		virtual void recordBufferToImage(VkCommandBuffer commandBuffer) override;
		virtual void recordImageToBuffer(VkCommandBuffer commandBuffer) override;
		virtual bool mayBeWrittenByShader() override;

		virtual std::shared_ptr<Buffer> buffer() const override;
		virtual size_t bufferOffset() const override;
		virtual size_t bufferBytesPerRow() const override;

		virtual std::shared_ptr<Heap> heap() override;
		virtual size_t heapOffset() override;
		virtual size_t allocatedSize() override;
		virtual void makeAliasable() override;
		virtual bool isAliasable() override;

		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, const void* bytes, size_t bytesPerRow) override;
		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, size_t slice, const void* bytes, size_t bytesPerRow, size_t bytesPerImage) override;
		virtual void getBytes(Indium::Region region, size_t mipmapLevel, void* bytes, size_t bytesPerRow) override;
		virtual void getBytes(Indium::Region region, size_t mipmapLevel, size_t slice, void* bytes, size_t bytesPerRow, size_t bytesPerImage) override;

		/**
		 * replaceRegion and getBytes for a view of this texture: `accessFormat` is the pixel format the caller
		 * sees the texels in (a view's), and the level and slice are this texture's own.
		 */
		void uploadRegion(Indium::Region region, size_t mipmapLevel, size_t slice, PixelFormat accessFormat, const void* bytes, size_t bytesPerRow, size_t bytesPerImage);
		void uploadExtendedRange(Indium::Region region, size_t mipmapLevel, size_t slice, const void* bytes, size_t bytesPerRow, size_t bytesPerImage);
		void downloadRegion(Indium::Region region, size_t mipmapLevel, size_t slice, PixelFormat accessFormat, void* bytes, size_t bytesPerRow, size_t bytesPerImage);
	};
};
