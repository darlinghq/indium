#pragma once

#include <vulkan/vulkan.h>

#include <indium/depth-stencil.hpp>

namespace Indium {
	class PrivateDevice;

	class PrivateDepthStencilState: public DepthStencilState {
	protected:
		std::shared_ptr<PrivateDevice> _privateDevice = nullptr;

	public:
		explicit PrivateDepthStencilState(std::shared_ptr<PrivateDevice> device, const DepthStencilDescriptor& descriptor);
		virtual ~PrivateDepthStencilState();

		virtual std::shared_ptr<Indium::Device> device() override;

		INDIUM_PROPERTY_REF(DepthStencilDescriptor, d, D,escriptor);
	};
};
