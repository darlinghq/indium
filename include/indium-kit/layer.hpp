#pragma once

#include <indium/indium.hpp>

#include <vulkan/vulkan.h>

namespace IndiumKit {
	// these are actually from CoreAnimation

	// this is supposed to be CAMetalDrawable
	class Drawable: virtual public Indium::Drawable {
	public:
		virtual ~Drawable() = 0;

		virtual std::shared_ptr<Indium::Texture> texture() = 0;
	};

	// this is supposed to be CAMetalLayer
	class Layer {
	public:
		virtual ~Layer() = 0;

		virtual std::shared_ptr<Drawable> nextDrawable() = 0;

		static std::shared_ptr<Layer> make(VkSurfaceKHR surface, std::shared_ptr<Indium::Device> device, size_t framebufferWidth, size_t framebufferHeight);

		virtual Indium::PixelFormat pixelFormat() = 0;
		virtual void setPixelFormat(Indium::PixelFormat pixelFormat) = 0;
	};
};
