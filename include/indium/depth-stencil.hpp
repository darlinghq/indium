#pragma once

#include <indium/types.hpp>

#include <memory>
#include <optional>

namespace Indium {
	class Device;

	struct StencilDescriptor {
		CompareFunction stencilCompareFunction = CompareFunction::Always;
		StencilOperation stencilFailureOperation = StencilOperation::Keep;
		StencilOperation depthFailureOperation = StencilOperation::Keep;
		StencilOperation depthStencilPassOperation = StencilOperation::Keep;
		uint32_t readMask = UINT32_MAX;
		uint32_t writeMask = UINT32_MAX;
	};

	struct DepthStencilDescriptor {
		CompareFunction depthCompareFunction = CompareFunction::Always;
		bool depthWriteEnabled = false;
		std::optional<StencilDescriptor> frontFaceStencil = std::nullopt;
		std::optional<StencilDescriptor> backFaceStencil = std::nullopt;
	};

	class DepthStencilState {
	public:
		virtual ~DepthStencilState() = 0;

		virtual std::shared_ptr<Device> device() = 0;
	};
};
