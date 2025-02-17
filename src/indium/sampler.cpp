#include <indium/sampler.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

Indium::SamplerState::~SamplerState() {};

Indium::PrivateSamplerState::PrivateSamplerState(std::shared_ptr<PrivateDevice> device, const SamplerDescriptor& descriptor):
	_privateDevice(device),
	_descriptor(descriptor)
{
	VkSamplerCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.magFilter = samplerMinMagFilterToVkFilter(descriptor.magFilter);
	info.minFilter = samplerMinMagFilterToVkFilter(descriptor.minFilter);
	info.mipmapMode = samplerMipFilterToVkSamplerMipmapMode(descriptor.mipFilter);
	info.addressModeU = samplerAddressModeToVkSamplerAddressMode(descriptor.sAddressMode);
	info.addressModeV = samplerAddressModeToVkSamplerAddressMode(descriptor.tAddressMode);
	info.addressModeW = samplerAddressModeToVkSamplerAddressMode(descriptor.rAddressMode);
	info.mipLodBias = 0; // not sure where to get this from
	info.anisotropyEnable = (descriptor.maxAnisotropy > 1) ? VK_TRUE : VK_FALSE;
	info.maxAnisotropy = descriptor.maxAnisotropy;
	info.compareEnable = descriptor.normalizedCoordinates ? VK_TRUE : VK_FALSE; // FIXME: this assumes that the implementation defers to whatever the shader state specifies
	info.compareOp = compareFunctionToVkCompareOp(descriptor.compareFunction);
	info.minLod = descriptor.normalizedCoordinates ? descriptor.lodMinClamp : 0;
	info.maxLod = descriptor.normalizedCoordinates ? descriptor.lodMaxClamp : 0;
	info.borderColor = samplerBorderColorToVkBorderColor(descriptor.borderColor);
	info.unnormalizedCoordinates = descriptor.normalizedCoordinates ? VK_FALSE : VK_TRUE;

	if (DynamicVK::vkCreateSampler(_privateDevice->device(), &info, nullptr, &_sampler) != VK_SUCCESS) {
		// TODO
		abort();
	}
};

Indium::PrivateSamplerState::~PrivateSamplerState() {
	DynamicVK::vkDestroySampler(_privateDevice->device(), _sampler, nullptr);
};

std::shared_ptr<Indium::Device> Indium::PrivateSamplerState::device() {
	return _privateDevice;
};

std::shared_ptr<Indium::PrivateSamplerState> Indium::PrivateSamplerState::cloneWithClamps(float lodMinClamp, float lodMaxClamp) {
	SamplerDescriptor desc2 = _descriptor;
	desc2.lodMinClamp = lodMinClamp;
	desc2.lodMaxClamp = lodMaxClamp;
	return std::make_shared<PrivateSamplerState>(_privateDevice, desc2);
};
