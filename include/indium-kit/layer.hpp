#pragma once

#include <indium/indium.hpp>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>

namespace IndiumKit {
	// these are actually from CoreAnimation

	// this is supposed to be CAMetalDrawable
	class Drawable: virtual public Indium::Drawable {
	public:
		virtual ~Drawable() = 0;

		virtual std::shared_ptr<Indium::Texture> texture() = 0;

		/**
		 * Sets a function to call once after present() has handed the image to the presentation
		 * engine. The argument is true when the image was queued for display and false when the
		 * presentation failed (for example because the surface was lost). It runs on the thread
		 * that called present().
		 */
		virtual void setPresentCallback(std::function<void(bool presented)> callback) = 0;
	};

	/**
	 * Creates a presentation surface for an Xlib window. `display` is the Display* and `window`
	 * the Window the surface presents to. Returns VK_NULL_HANDLE when Vulkan cannot present to
	 * Xlib windows (the loader lacks VK_KHR_xlib_surface) or the call fails.
	 */
	VkSurfaceKHR createXlibSurface(void* display, unsigned long window);

	/**
	 * Creates a presentation surface for a Wayland surface (`display` is the wl_display*,
	 * `surface` the wl_surface*). Returns VK_NULL_HANDLE when Vulkan cannot present to Wayland
	 * surfaces (the loader lacks VK_KHR_wayland_surface) or the call fails.
	 */
	VkSurfaceKHR createWaylandSurface(void* display, void* surface);

	/** Destroys a surface that was never given to a Layer (a Layer destroys its own). */
	void destroySurface(VkSurfaceKHR surface);

	/**
	 * Whether `device` can present to `surface`: the device supports swapchains and the queue
	 * that renders can present to it. Call this before Layer::make() when more than one device
	 * exists, since the surface may be presentable by only some of them.
	 */
	bool canPresent(const std::shared_ptr<Indium::Device>& device, VkSurfaceKHR surface);

	// this is supposed to be CAMetalLayer
	class Layer {
	public:
		virtual ~Layer() = 0;

		/** Same as nextDrawable(1 second). */
		virtual std::shared_ptr<Drawable> nextDrawable() = 0;

		/**
		 * Waits up to `timeoutNanoseconds` (UINT64_MAX waits without limit) for the presentation
		 * engine to hand out an image. Returns null when none became available in time, or when
		 * the surface is no longer usable.
		 */
		virtual std::shared_ptr<Drawable> nextDrawable(uint64_t timeoutNanoseconds) = 0;

		/**
		 * Creates a layer that presents to `surface` and takes ownership of it when it succeeds.
		 * Returns null, leaving the surface to the caller, when the surface cannot be presented to
		 * with the default pixel format by `device`.
		 */
		static std::shared_ptr<Layer> make(VkSurfaceKHR surface, std::shared_ptr<Indium::Device> device, size_t framebufferWidth, size_t framebufferHeight);

		virtual Indium::PixelFormat pixelFormat() = 0;
		/** Returns false, changing nothing, when the surface cannot present this format. */
		virtual bool setPixelFormat(Indium::PixelFormat pixelFormat) = 0;

		/**
		 * The size of the images in pixels. The swapchain is recreated by the next nextDrawable()
		 * call. When the surface dictates its own size the images follow it instead, as
		 * framebufferWidth() and framebufferHeight() report after that call.
		 */
		virtual void setFramebufferSize(size_t width, size_t height) = 0;
		virtual size_t framebufferWidth() = 0;
		virtual size_t framebufferHeight() = 0;

		/**
		 * The number of drawables the application can hold at once (acquired and not yet presented).
		 * The swapchain gets more images than that where the presentation engine needs some for itself,
		 * and fewer where the surface limits the count. Takes effect when the swapchain is next created.
		 */
		virtual void setImageCount(uint32_t count) = 0;

		/**
		 * Whether the images are only used as render targets. Without it they can also be sampled,
		 * written by shaders and copied to or from, as far as the surface allows.
		 */
		virtual void setFramebufferOnly(bool framebufferOnly) = 0;

		/** Whether presentation waits for the display's vertical blank (the default). */
		virtual void setDisplaySyncEnabled(bool enabled) = 0;
	};
};
