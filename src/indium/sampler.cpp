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
	// Metal applies the compare function only to comparison sampling (sample_compare and gather_compare on a depth
	// texture, OpImage*Dref* in SPIR-V), and ignores it for every other read, whatever it is set to. Vulkan applies
	// a comparison sampler's to every read, and what one returns for a read that is not a Dref is undefined (lavapipe
	// returns the comparison). So this sampler compares nothing, and comparisonSampler() is the one bound where the
	// shader compares.
	info.compareEnable = VK_FALSE;
	info.compareOp = compareFunctionToVkCompareOp(descriptor.compareFunction);
	if (!descriptor.normalizedCoordinates) {
		// required by Vulkan for unnormalized coordinates
		info.minLod = 0;
		info.maxLod = 0;
	} else if (descriptor.mipFilter == SamplerMipFilter::NotMipmapped) {
		// Metal samples only the base level without a mip filter. Vulkan has no such mode; clamping
		// the LOD to [0, 0.25] reads level 0 and still selects between the min and mag filters, as
		// the Vulkan specification suggests for this case.
		info.minLod = 0;
		info.maxLod = 0.25f;
	} else {
		info.minLod = descriptor.lodMinClamp;
		info.maxLod = descriptor.lodMaxClamp;
	}
	info.borderColor = samplerBorderColorToVkBorderColor(descriptor.borderColor);
	info.unnormalizedCoordinates = descriptor.normalizedCoordinates ? VK_FALSE : VK_TRUE;

	if (DynamicVK::vkCreateSampler(_privateDevice->device(), &info, nullptr, &_sampler) != VK_SUCCESS) {
		// TODO
		abort();
	}
	_createInfo = info;
};

Indium::PrivateSamplerState::~PrivateSamplerState() {
	if (_comparisonSampler != VK_NULL_HANDLE) {
		DynamicVK::vkDestroySampler(_privateDevice->device(), _comparisonSampler, nullptr);
	}
	DynamicVK::vkDestroySampler(_privateDevice->device(), _sampler, nullptr);
};

VkSampler Indium::PrivateSamplerState::comparisonSampler() {
	// Vulkan does not allow comparison with unnormalized coordinates
	if (!_descriptor.normalizedCoordinates) {
		return _sampler;
	}
	std::call_once(_comparisonOnce, [this]() {
		VkSamplerCreateInfo info = _createInfo;
		info.compareEnable = VK_TRUE;
		if (DynamicVK::vkCreateSampler(_privateDevice->device(), &info, nullptr, &_comparisonSampler) != VK_SUCCESS) {
			// the same failure as creating the sampler in the constructor, which has no way to report it either
			abort();
		}
	});
	return _comparisonSampler;
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
