#pragma once

#include <vulkan/vulkan.h>

#include <mutex>

#include <indium/sampler.hpp>

namespace Indium {
	class PrivateDevice;

	class PrivateSamplerState: public SamplerState {
	protected:
		std::shared_ptr<PrivateDevice> _privateDevice = nullptr;
		SamplerDescriptor _descriptor;

	public:
		explicit PrivateSamplerState(std::shared_ptr<PrivateDevice> device, const SamplerDescriptor& descriptor);
		virtual ~PrivateSamplerState();

		virtual std::shared_ptr<Indium::Device> device() override;

		std::shared_ptr<PrivateSamplerState> cloneWithClamps(float lodMinClamp, float lodMaxClamp);

		/**
		 * The sampler for a binding the shader samples a depth comparison through: the descriptor's compare function
		 * enabled. sampler() has it disabled, which is what every other sampling needs. Created on first use; for
		 * unnormalized coordinates, which Vulkan allows no comparison with, it is sampler().
		 */
		VkSampler comparisonSampler();

		/** Whether the descriptor's compare function is other than Never. */
		bool hasCompareFunction() const { return _descriptor.compareFunction != CompareFunction::Never; }

		INDIUM_PROPERTY(VkSampler, s, S,ampler) = VK_NULL_HANDLE;

	private:
		VkSamplerCreateInfo _createInfo {};
		std::once_flag _comparisonOnce;
		VkSampler _comparisonSampler = VK_NULL_HANDLE;
	};
};
