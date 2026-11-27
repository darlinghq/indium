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
