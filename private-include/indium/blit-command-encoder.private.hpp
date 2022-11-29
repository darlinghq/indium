#pragma once

#include <indium/blit-command-encoder.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <variant>
#include <vector>

namespace Indium {
	class PrivateCommandBuffer;
	class PrivateDevice;

	class PrivateBlitCommandEncoder: public BlitCommandEncoder {
	private:
		// the command buffer always outlives us
		std::weak_ptr<PrivateCommandBuffer> _privateCommandBuffer;
		std::shared_ptr<PrivateDevice> _privateDevice;

	public:
		PrivateBlitCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer);
		~PrivateBlitCommandEncoder();

		virtual void generateMipmapsForTexture(std::shared_ptr<Texture> texture) override;

		virtual void endEncoding() override;
	};
};
