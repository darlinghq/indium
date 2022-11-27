#include <indium/depth-stencil.private.hpp>
#include <indium/device.private.hpp>

Indium::DepthStencilState::~DepthStencilState() {};

Indium::PrivateDepthStencilState::PrivateDepthStencilState(std::shared_ptr<PrivateDevice> device, const DepthStencilDescriptor& descriptor):
	_privateDevice(device),
	_descriptor(descriptor)
	{};

Indium::PrivateDepthStencilState::~PrivateDepthStencilState() {};

std::shared_ptr<Indium::Device> Indium::PrivateDepthStencilState::device() {
	return _privateDevice;
};
