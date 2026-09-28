#include <indium/sampler.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>

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
	// Metal clamps the descriptor's maxAnisotropy to what the device supports,
	// and Vulkan requires maxAnisotropy to be no greater than the device's limit
	// whenever anisotropyEnable is set. A value past the limit is not clamped by
	// the driver: on asahi maxAnisotropy=1024 against a limit of 16 silently
	// falls back to isotropic filtering, which is worse than either answer.
	auto maxAnisotropy = std::max<size_t>(1, std::min<size_t>(
		descriptor.maxAnisotropy, _privateDevice->properties().limits.maxSamplerAnisotropy));

	info.mipLodBias = 0; // not sure where to get this from
	info.anisotropyEnable = (maxAnisotropy > 1) ? VK_TRUE : VK_FALSE;
	info.maxAnisotropy = maxAnisotropy;
	info.compareEnable = VK_FALSE;
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
