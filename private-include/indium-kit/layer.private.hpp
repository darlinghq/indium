#pragma once

#include <indium-kit/layer.hpp>

#include <indium-kit/hdr.private.hpp>

#include <indium/hdr-compat.hpp>
#include <indium/indium.private.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace IndiumKit {
	class PrivateLayer;

	// One generation of a layer's swapchain. Drawables keep the generation they were acquired from alive,
	// so a swapchain replaced by a resize is destroyed only once its last drawable is gone.
	struct SwapchainState {
		std::shared_ptr<Indium::PrivateDevice> device;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		std::vector<VkImage> images;
		std::vector<VkImageView> imageViews;
		size_t width = 0;
		size_t height = 0;
		Indium::PixelFormat pixelFormat = Indium::PixelFormat::BGRA8Unorm;
		VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		bool framebufferOnly = true;

		// A layer in an extended-range pixel format (see indium/extended-range.private.hpp) has no swapchain image of
		// its format: it renders to a texture of its own for each image and converts that into the image when presenting.
		// The conversion of every image is recorded once, here.
		struct ExtendedRangePresentation {
			std::vector<std::shared_ptr<Indium::Texture>> textures;
			VkCommandPool commandPool = VK_NULL_HANDLE;
			std::vector<VkCommandBuffer> commandBuffers;
			VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
			std::vector<std::shared_ptr<Indium::BinarySemaphore>> presentSemaphores;
		};
		ExtendedRangePresentation extendedRange;

		~SwapchainState();
	};

	class PrivateDrawable: public Drawable, public Indium::PrivateTexture {
	private:
		std::shared_ptr<PrivateLayer> _layer;
		std::shared_ptr<SwapchainState> _state;
		uint32_t _swapchainImageIndex;
		std::function<void(bool)> _presentCallback;
		bool _presented = false;
		// the semaphore the presentation engine signals when the image is free, of a layer whose drawables render to
		// a texture of their own (the conversion into the image waits for it)
		std::shared_ptr<Indium::BinarySemaphore> _acquireSemaphore;

	public:
		PrivateDrawable(std::shared_ptr<PrivateLayer> layer, std::shared_ptr<SwapchainState> state, uint32_t swapchainImageIndex, std::shared_ptr<Indium::BinarySemaphore> extraWaitSemaphore);
		~PrivateDrawable();

		virtual std::shared_ptr<Indium::Texture> texture() override;
		virtual void setPresentCallback(std::function<void(bool)> callback) override;

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
		virtual Indium::TextureUsage usage() const override;
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
		size_t _width;
		size_t _height;
		uint32_t _imageCount = 3;
		bool _framebufferOnly = true;
		bool _displaySync = true;
		HdrMetadata _hdrMetadata;
		ColorSpace _colorSpace = ColorSpace::SRGB;
		bool _extendedDynamicRange = false;

		// guards the swapchain generation and the settings above
		std::mutex _stateMutex;
		std::shared_ptr<SwapchainState> _state;
		bool _outdated = false;

		// An image that has been acquired from the swapchain but is not usable yet (the presentation engine still
		// holds it), which the next nextDrawable() call goes on waiting for; see nextDrawable().
		std::mutex _acquireMutex;
		VkFence _acquireFence = VK_NULL_HANDLE;
		bool _acquirePending = false;
		uint32_t _acquiredIndex = 0;
		std::shared_ptr<Indium::BinarySemaphore> _acquiredSemaphore;
		std::shared_ptr<SwapchainState> _acquiredState;

		// Presents that VK_KHR_present_wait can tell about: the waiter thread waits for each in turn and then runs the
		// presenter's callback, so that it runs when the image reached the display.
		struct PendingPresent {
			std::shared_ptr<SwapchainState> state;
			uint64_t id;
			std::function<void(bool)> callback;
		};
		std::atomic<uint64_t> _presentCounter { 0 };

		// Shared with the waiter thread, which can outlive the layer when the layer's last reference is
		// released from one of its callbacks.
		struct Waiter {
			std::mutex mutex;
			std::condition_variable condition;
			std::deque<PendingPresent> pending;
			bool stop = false;
		};
		std::shared_ptr<Waiter> _waiterState = std::make_shared<Waiter>();
		std::thread _waiter;

		static void runWaiter(std::shared_ptr<Waiter> waiter, std::shared_ptr<Indium::PrivateDevice> device);

		/** The color space the requested HDR transfer function needs, or sRGB for SDR content. */
		static VkColorSpaceKHR hdrColorSpace(HdrTransferFunction transfer);

		/** How an extended-range pixel format reaches the surface: the swapchain it is converted into. */
		struct ExtendedRangeTarget {
			VkFormat format;
			VkColorSpaceKHR colorSpace;
			// the swapchain takes linear light, which the gamma encoded values of a non-sRGB format are converted to
			bool linearLight;
		};

		/**
		 * How a layer's content reaches the surface. `intermediate` layers render to textures of their own and
		 * convert them into the swapchain image (see SwapchainState::ExtendedRangePresentation); the others
		 * render to the swapchain image. `decode` is for the gamma encoded values of an extended-range pixel
		 * format that go to a linear swapchain.
		 */
		struct PresentationTarget {
			VkFormat format = VK_FORMAT_UNDEFINED;
			VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
			bool intermediate = false;
			bool decode = false;
		};
		bool choosePresentationTarget(Indium::PixelFormat pixelFormat, HdrTransferFunction transfer, ColorSpace colorSpace, bool extendedDynamicRange,
		                              const std::vector<VkSurfaceFormatKHR>& surfaceFormats, PresentationTarget& target);
		std::vector<VkSurfaceFormatKHR> surfaceFormats();
		bool chooseExtendedRangeTarget(Indium::PixelFormat pixelFormat, const std::vector<VkSurfaceFormatKHR>& surfaceFormats, ExtendedRangeTarget& target);
		bool createExtendedRangePresentation(const std::shared_ptr<SwapchainState>& state, bool decode);

		/** Applies the current metadata to a swapchain whose color space can carry it. */
		void applyHdrMetadata(const std::shared_ptr<SwapchainState>& state);

		// Builds a swapchain for the current settings, or returns null when the surface cannot present them.
		std::shared_ptr<SwapchainState> createSwapchain(VkSwapchainKHR oldSwapchain);

	public:
		PrivateLayer(VkSurfaceKHR surface, std::shared_ptr<Indium::Device> device, size_t framebufferWidth, size_t framebufferHeight);
		~PrivateLayer();

		virtual std::shared_ptr<Drawable> nextDrawable() override;
		virtual std::shared_ptr<Drawable> nextDrawable(uint64_t timeoutNanoseconds) override;
		virtual Indium::PixelFormat pixelFormat() override;
		virtual bool setPixelFormat(Indium::PixelFormat pixelFormat) override;
		virtual void setFramebufferSize(size_t width, size_t height) override;
		virtual size_t framebufferWidth() override;
		virtual size_t framebufferHeight() override;
		virtual void setImageCount(uint32_t count) override;
		virtual void setFramebufferOnly(bool framebufferOnly) override;
		virtual void setDisplaySyncEnabled(bool enabled) override;
		virtual void setHdrMetadata(const HdrMetadata& metadata) override;
		virtual bool setColorSpace(ColorSpace colorSpace, bool extendedDynamicRange) override;

		std::shared_ptr<Indium::Device> device();

		// Whether presented callbacks wait for the display (VK_KHR_present_wait), instead of running when
		// the image was handed to the presentation engine.
		bool waitsForPresents();

		// Numbers a present for VkPresentIdKHR.
		uint64_t nextPresentID() { return ++_presentCounter; }

		// Runs `callback(true)` once present `id` of `state` reached the display (or its wait gave up).
		void callWhenPresented(std::shared_ptr<SwapchainState> state, uint64_t id, std::function<void(bool)> callback);

		// Whether `state` is the layer's current swapchain, not one that a resize or a setting replaced.
		bool isCurrent(const std::shared_ptr<SwapchainState>& state);

		// Called when presentation reported the swapchain as out of date or suboptimal.
		void markOutdated(const std::shared_ptr<SwapchainState>& state);
	};
};
