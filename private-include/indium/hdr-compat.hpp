#pragma once

#include <vulkan/vulkan.h>

// VK_EXT_hdr_metadata and the HDR color spaces are old extensions, but they are still optional in the
// Vulkan headers a distribution installs. Define the small part of their ABI Indium uses when a header
// is too old to provide it, so that the rest of the code can use the standard names either way.

#ifndef VK_EXT_hdr_metadata
	#define VK_EXT_hdr_metadata 1
	#define VK_EXT_HDR_METADATA_SPEC_VERSION 3
	#define VK_EXT_HDR_METADATA_EXTENSION_NAME "VK_EXT_hdr_metadata"
	#define VK_STRUCTURE_TYPE_HDR_METADATA_EXT ((VkStructureType)1000105000)

	typedef struct VkXYColorEXT {
		float x;
		float y;
	} VkXYColorEXT;

	typedef struct VkHdrMetadataEXT {
		VkStructureType sType;
		const void* pNext;
		VkXYColorEXT displayPrimaryRed;
		VkXYColorEXT displayPrimaryGreen;
		VkXYColorEXT displayPrimaryBlue;
		VkXYColorEXT whitePoint;
		float maxLuminance;
		float minLuminance;
		float maxContentLightLevel;
		float maxFrameAverageLightLevel;
	} VkHdrMetadataEXT;

	typedef void (VKAPI_PTR *PFN_vkSetHdrMetadataEXT)(
		VkDevice device, uint32_t swapchainCount, const VkSwapchainKHR* pSwapchains,
		const VkHdrMetadataEXT* pMetadata);
#endif

#ifndef VK_COLOR_SPACE_HDR10_ST2084_EXT
	#define VK_COLOR_SPACE_HDR10_ST2084_EXT ((VkColorSpaceKHR)1000104008)
#endif

#ifndef VK_COLOR_SPACE_HDR10_HLG_EXT
	#define VK_COLOR_SPACE_HDR10_HLG_EXT ((VkColorSpaceKHR)1000104010)
#endif

#ifndef VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME
	#define VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME "VK_EXT_swapchain_colorspace"
#endif
