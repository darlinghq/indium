#pragma once

#include <indium-kit/layer.hpp>

#include <indium/hdr-compat.hpp>

namespace IndiumKit {
	inline VkColorSpaceKHR hdrColorSpace(HdrTransferFunction transfer) {
		switch (transfer) {
			case HdrTransferFunction::PQ:  return VK_COLOR_SPACE_HDR10_ST2084_EXT;
			case HdrTransferFunction::HLG: return VK_COLOR_SPACE_HDR10_HLG_EXT;
			case HdrTransferFunction::None: break;
		}
		return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	}

	/**
	 * Vulkan has no way to say that a field of VkHdrMetadataEXT is unknown, and a driver forwards
	 * the values as they are (the Wayland WSI turns them into color-management requests, where a
	 * mastering luminance range whose maximum does not exceed its minimum is a protocol error). So
	 * metadata is only applied when it states a luminance range, and unknown mastering primaries
	 * are filled in with those of the BT.2100 color spaces a PQ or HLG swapchain declares
	 * (BT.2020 primaries, D65 white). Content light levels that are zero stay zero: they are
	 * undefined in that case.
	 */
	inline bool completeHdrMetadata(const HdrMetadata& metadata, HdrMetadata& result) {
		if (metadata.transfer == HdrTransferFunction::None || !(metadata.maxLuminance > metadata.minLuminance)
		    || metadata.minLuminance < 0) {
			return false;
		}
		result = metadata;
		bool primariesKnown = false;
		for (const auto* v: { &metadata.displayPrimaryRed, &metadata.displayPrimaryGreen,
		                      &metadata.displayPrimaryBlue, &metadata.whitePoint }) {
			primariesKnown = primariesKnown || (*v)[0] != 0 || (*v)[1] != 0;
		}
		if (!primariesKnown) {
			result.displayPrimaryRed = { 0.708f, 0.292f };
			result.displayPrimaryGreen = { 0.170f, 0.797f };
			result.displayPrimaryBlue = { 0.131f, 0.046f };
			result.whitePoint = { 0.3127f, 0.3290f };
		}
		return true;
	}

	inline VkHdrMetadataEXT hdrMetadataToVulkan(const HdrMetadata& metadata) {
		VkHdrMetadataEXT result {};
		result.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
		result.displayPrimaryRed = { metadata.displayPrimaryRed[0], metadata.displayPrimaryRed[1] };
		result.displayPrimaryGreen = { metadata.displayPrimaryGreen[0], metadata.displayPrimaryGreen[1] };
		result.displayPrimaryBlue = { metadata.displayPrimaryBlue[0], metadata.displayPrimaryBlue[1] };
		result.whitePoint = { metadata.whitePoint[0], metadata.whitePoint[1] };
		result.maxLuminance = metadata.maxLuminance;
		result.minLuminance = metadata.minLuminance;
		result.maxContentLightLevel = metadata.maxContentLightLevel;
		result.maxFrameAverageLightLevel = metadata.maxFrameAverageLightLevel;
		return result;
	}
};
