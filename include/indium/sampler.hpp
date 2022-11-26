#pragma once

#include <indium/base.hpp>
#include <indium/types.hpp>

#include <limits>
#include <memory>

namespace Indium {
	class Device;
	class Buffer;

	struct SamplerDescriptor {
		SamplerMinMagFilter minFilter = SamplerMinMagFilter::Nearest;
		SamplerMinMagFilter magFilter = SamplerMinMagFilter::Nearest;
		SamplerMipFilter mipFilter = SamplerMipFilter::NotMipmapped;
		size_t maxAnisotropy = 1;
		SamplerAddressMode sAddressMode = SamplerAddressMode::ClampToEdge;
		SamplerAddressMode tAddressMode = SamplerAddressMode::ClampToEdge;
		SamplerAddressMode rAddressMode = SamplerAddressMode::ClampToEdge;
		SamplerBorderColor borderColor = SamplerBorderColor::TransparentBlack;
		bool normalizedCoordinates = true;
		float lodMinClamp = 0;
		float lodMaxClamp = std::numeric_limits<float>::max();
		bool supportArgumentBuffers = false;
		CompareFunction compareFunction = CompareFunction::Never;
	};

	class SamplerState {
	public:
		virtual ~SamplerState() = 0;

		virtual std::shared_ptr<Device> device() = 0;
	};
};
