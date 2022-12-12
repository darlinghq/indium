#pragma once

#include <indium-kit/layer.hpp>

#include <indium/indium.private.hpp>

namespace IndiumKit {
	class PrivateLayer;

	class PrivateDrawable: public Drawable, public Indium::PrivateTexture {
	private:
		std::shared_ptr<PrivateLayer> _layer;
		VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
		uint32_t _swapchainImageIndex;
		VkImageView _imageView = VK_NULL_HANDLE;
		VkImage _image = VK_NULL_HANDLE;
		size_t _width;
		size_t _height;
		Indium::PixelFormat _pixelFormat;

	public:
		PrivateDrawable(std::shared_ptr<PrivateLayer> layer, VkSwapchainKHR swapchain, uint32_t swapchainImageIndex, VkImageView imageView, VkImage image, std::shared_ptr<Indium::BinarySemaphore> extraWaitSemaphore, size_t width, size_t height, Indium::PixelFormat pixelFormat);
		~PrivateDrawable();

		virtual std::shared_ptr<Indium::Texture> texture() override;

		virtual VkImageView imageView() override;
		virtual VkImage image() override;

		virtual VkImageLayout imageLayout() override;
		virtual void present() override;

		virtual Indium::TextureType textureType() const override;
		virtual Indium::PixelFormat pixelFormat() const override;
		virtual size_t width() const override;
		virtual size_t height() const override;
		virtual size_t depth() const override;
		virtual size_t mipmapLevelCount() const override;
		virtual size_t arrayLength() const override;
		virtual size_t sampleCount() const override;
		virtual bool framebufferOnly() const override;
		virtual bool allowGPUOptimizedContents() const override;
		virtual bool shareable() const override;
		virtual Indium::TextureSwizzleChannels swizzle() const override;

		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, const void* bytes, size_t bytesPerRow) override;
		virtual void replaceRegion(Indium::Region region, size_t mipmapLevel, size_t slice, const void* bytes, size_t bytesPerRow, size_t bytesPerImage) override;
	};

	class PrivateLayer: public Layer, public std::enable_shared_from_this<PrivateLayer> {
	private:
		std::shared_ptr<Indium::Device> _device;
		Indium::PixelFormat _pixelFormat = Indium::PixelFormat::BGRA8Unorm;
		VkSurfaceKHR _surface = VK_NULL_HANDLE;
		VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
		std::vector<VkImage> _images;
		std::vector<VkImageView> _imageViews;
		size_t _width;
		size_t _height;

	public:
		PrivateLayer(VkSurfaceKHR surface, std::shared_ptr<Indium::Device> device, size_t framebufferWidth, size_t framebufferHeight);
		~PrivateLayer();

		virtual std::shared_ptr<Drawable> nextDrawable() override;
		virtual Indium::PixelFormat pixelFormat() override;
		virtual void setPixelFormat(Indium::PixelFormat pixelFormat) override;

		std::shared_ptr<Indium::Device> device();
	};
};
