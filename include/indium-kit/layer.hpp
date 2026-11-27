#pragma once

#include <indium/indium.hpp>

#include <vulkan/vulkan.h>

#include <array>
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

	/** The transfer function of the HDR content presented by a layer. */
	enum class HdrTransferFunction: uint8_t {
		None,
		/** SMPTE ST 2084 (PQ), as used by HDR10. */
		PQ,
		/** ITU-R BT.2100 hybrid log-gamma. */
		HLG,
	};

	/**
	 * HDR metadata for a layer's swapchain, obtained from CAEDRMetadata on the guest side. Any field
	 * left at zero is unknown, which Vulkan also represents as zero; the transfer function selects the
	 * swapchain's color space. Chromaticities are CIE 1931 x/y values in [0, 1], luminances in nits.
	 */
	struct HdrMetadata {
		HdrTransferFunction transfer = HdrTransferFunction::None;
		std::array<float, 2> displayPrimaryRed { 0, 0 };
		std::array<float, 2> displayPrimaryGreen { 0, 0 };
		std::array<float, 2> displayPrimaryBlue { 0, 0 };
		std::array<float, 2> whitePoint { 0, 0 };
		float maxLuminance = 0;
		float minLuminance = 0;
		float maxContentLightLevel = 0;
		float maxFrameAverageLightLevel = 0;
	};

	inline bool operator==(const HdrMetadata& lhs, const HdrMetadata& rhs) {
		return lhs.transfer == rhs.transfer &&
		       lhs.displayPrimaryRed == rhs.displayPrimaryRed &&
		       lhs.displayPrimaryGreen == rhs.displayPrimaryGreen &&
		       lhs.displayPrimaryBlue == rhs.displayPrimaryBlue &&
		       lhs.whitePoint == rhs.whitePoint &&
		       lhs.maxLuminance == rhs.maxLuminance &&
		       lhs.minLuminance == rhs.minLuminance &&
		       lhs.maxContentLightLevel == rhs.maxContentLightLevel &&
		       lhs.maxFrameAverageLightLevel == rhs.maxFrameAverageLightLevel;
	}

	inline bool operator!=(const HdrMetadata& lhs, const HdrMetadata& rhs) {
		return !(lhs == rhs);
	}

	/**
	 * The color space of the values an application writes to a layer's drawables, which is what the
	 * swapchain has to be told so that the presentation interprets them right (CAMetalLayer.colorspace).
	 * See Layer::setColorSpace() for what happens when the surface cannot present a color space itself.
	 */
	enum class ColorSpace: uint8_t {
		/** sRGB primaries and transfer function (the default): VK_COLOR_SPACE_SRGB_NONLINEAR_KHR. */
		SRGB,
		/** sRGB primaries and transfer function extended to values outside 0 to 1: VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT. */
		ExtendedSRGB,
		/** sRGB primaries, linear light, 1.0 is the SDR white (scRGB): VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT. */
		ExtendedLinearSRGB,
		/** Display P3 primaries, linear light: VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT. */
		ExtendedLinearDisplayP3,
		/** BT.2020 primaries, linear light: VK_COLOR_SPACE_BT2020_LINEAR_EXT. */
		ExtendedLinearITUR2020,
	};

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

		/**
		 * Sets the HDR metadata applied to the swapchain with vkSetHdrMetadataEXT. When the transfer
		 * function changes, the swapchain is recreated so that it uses the matching HDR color space;
		 * if the surface cannot present that color space, the layer falls back to sRGB. Changing only
		 * the values updates the metadata of the current swapchain without recreating it. Metadata is
		 * ignored when the Vulkan device does not support VK_EXT_hdr_metadata.
		 */
		virtual void setHdrMetadata(const HdrMetadata& metadata) = 0;

		/**
		 * Sets the color space of the values the application writes to the drawables, and whether
		 * the layer shows extended dynamic range (values above 1.0 brighter than the SDR white;
		 * CAMetalLayer.wantsExtendedDynamicRangeContent). Returns false, changing nothing, when the
		 * surface cannot present the layer's current pixel format that way. Takes effect when the
		 * swapchain is next created. When the pixel format and the color space both change, each is
		 * checked against the other's current value, so call first the one that is valid with the old
		 * value of the other (a surface may offer a format only together with a color space).
		 *
		 * A surface that offers the pixel format in the matching Vulkan color space (through
		 * VK_EXT_swapchain_colorspace) presents the values itself. Otherwise, for the color spaces
		 * with sRGB primaries (ExtendedSRGB and ExtendedLinearSRGB), the layer renders to textures of
		 * its own and converts them to an 8-bit swapchain when presenting: the values are clamped to
		 * 0 to 1 and linear ones get the sRGB curve, so the colors shown are right but values beyond
		 * the range are not shown. The same is done for ExtendedLinearSRGB while `extendedDynamicRange`
		 * is false, as without it values beyond 0 to 1 must not reach the display. The other linear
		 * color spaces (Display P3, BT.2020) have no such conversion and are refused when the surface
		 * does not offer them. An HDR transfer function from setHdrMetadata() takes precedence.
		 */
		virtual bool setColorSpace(ColorSpace colorSpace, bool extendedDynamicRange) = 0;
	};
};
