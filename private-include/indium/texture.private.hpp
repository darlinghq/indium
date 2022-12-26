#pragma once

#include <vulkan/vulkan.h>

#include <indium/texture.hpp>
#include <indium/types.private.hpp>

#include <mutex>
#include <unordered_map>

namespace Indium {
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
	};

	class TextureView: public PrivateTexture {
		INDIUM_PREVENT_COPY(TextureView);

	private:
		std::shared_ptr<PrivateTexture> _original;
		VkImageView _imageView;
		PixelFormat _pixelFormat;
		TextureType _textureType;
		VkImageAspectFlags _imageAspect;
		Range<size_t> _levels;
		Range<size_t> _layers;
		TextureSwizzleChannels _swizzle;

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
		virtual bool allowGPUOptimizedContents() const override;
		virtual bool shareable() const override;
		virtual TextureSwizzleChannels swizzle() const override;

		virtual std::shared_ptr<Texture> parentTexture() const override;
		virtual size_t parentRelativeLevel() const override;
		virtual size_t parentRelativeSlice() const override;

		virtual VkImageView imageView() override;
		virtual VkImage image() override;
		virtual std::shared_ptr<Device> device() override;
		virtual VkImageLayout imageLayout() override;

		virtual const TimelineSemaphore& acquire(uint64_t& waitValue, std::shared_ptr<BinarySemaphore>& extraWaitSemaphore, uint64_t& signalValue) override;
		virtual void beginUpdatingPresentationSemaphore(std::shared_ptr<BinarySemaphore> presentationSemaphore) override;
		virtual void endUpdatingPresentationSemaphore() override;
		virtual std::shared_ptr<BinarySemaphore> synchronizePresentation() override;

		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, const void* bytes, size_t bytesPerRow) override;
		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, size_t slice, const void* bytes, size_t bytesPerRow, size_t bytesPerImage) override;
	};

	class ConcreteTexture: public PrivateTexture {
		INDIUM_PREVENT_COPY(ConcreteTexture);

	private:
		TextureDescriptor _descriptor;
		VkImage _image;
		VkImageView _imageView;
		VkDeviceMemory _memory;
		StorageMode _storageMode;

	public:
		ConcreteTexture(std::shared_ptr<PrivateDevice> device, const TextureDescriptor& descriptor);
		virtual ~ConcreteTexture();

		virtual TextureType textureType() const override;
		virtual PixelFormat pixelFormat() const override;
		virtual size_t width() const override;
		virtual size_t height() const override;
		virtual size_t depth() const override;
		virtual size_t mipmapLevelCount() const override;
		virtual size_t arrayLength() const override;
		virtual size_t sampleCount() const override;
		virtual bool framebufferOnly() const override;
		virtual bool allowGPUOptimizedContents() const override;
		virtual bool shareable() const override;
		virtual TextureSwizzleChannels swizzle() const override;

		virtual VkImageView imageView() override;
		virtual VkImage image() override;
		virtual VkImageLayout imageLayout() override;
		virtual size_t vulkanArrayLength() const override;

		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, const void* bytes, size_t bytesPerRow) override;
		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, size_t slice, const void* bytes, size_t bytesPerRow, size_t bytesPerImage) override;
	};
};
