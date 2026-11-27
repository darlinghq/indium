#include <indium-kit/hdr.private.hpp>

#include <cstdio>

static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
	using namespace IndiumKit;

	CHECK(hdrColorSpace(HdrTransferFunction::None) == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
	CHECK(hdrColorSpace(HdrTransferFunction::PQ) == VK_COLOR_SPACE_HDR10_ST2084_EXT);
	CHECK(hdrColorSpace(HdrTransferFunction::HLG) == VK_COLOR_SPACE_HDR10_HLG_EXT);

	HdrMetadata metadata;
	metadata.transfer = HdrTransferFunction::PQ;
	metadata.displayPrimaryRed = { 0.68f, 0.32f };
	metadata.displayPrimaryGreen = { 0.265f, 0.69f };
	metadata.displayPrimaryBlue = { 0.15f, 0.06f };
	metadata.whitePoint = { 0.3127f, 0.329f };
	metadata.maxLuminance = 1000;
	metadata.minLuminance = 0.5f;
	metadata.maxContentLightLevel = 800;
	metadata.maxFrameAverageLightLevel = 300;

	VkHdrMetadataEXT vk = hdrMetadataToVulkan(metadata);
	CHECK(vk.sType == VK_STRUCTURE_TYPE_HDR_METADATA_EXT);
	CHECK(vk.displayPrimaryRed.x == 0.68f && vk.displayPrimaryRed.y == 0.32f);
	CHECK(vk.displayPrimaryGreen.x == 0.265f && vk.displayPrimaryGreen.y == 0.69f);
	CHECK(vk.displayPrimaryBlue.x == 0.15f && vk.displayPrimaryBlue.y == 0.06f);
	CHECK(vk.whitePoint.x == 0.3127f && vk.whitePoint.y == 0.329f);
	CHECK(vk.maxLuminance == 1000 && vk.minLuminance == 0.5f);
	CHECK(vk.maxContentLightLevel == 800 && vk.maxFrameAverageLightLevel == 300);

	// Metadata without a luminance range is not applied: a driver would forward zeros as they are.
	HdrMetadata complete;
	HdrMetadata none;
	none.transfer = HdrTransferFunction::PQ;
	CHECK(!completeHdrMetadata(none, complete));
	CHECK(!completeHdrMetadata(HdrMetadata {}, complete));
	HdrMetadata flat = metadata;
	flat.maxLuminance = flat.minLuminance;
	CHECK(!completeHdrMetadata(flat, complete));

	// Known primaries are kept, unknown ones become those of the BT.2100 color spaces.
	CHECK(completeHdrMetadata(metadata, complete));
	CHECK(complete.displayPrimaryRed == metadata.displayPrimaryRed && complete.maxContentLightLevel == 800);
	HdrMetadata lumaOnly;
	lumaOnly.transfer = HdrTransferFunction::HLG;
	lumaOnly.minLuminance = 0.01f;
	lumaOnly.maxLuminance = 1000;
	CHECK(completeHdrMetadata(lumaOnly, complete));
	CHECK(complete.displayPrimaryRed == (std::array<float, 2> { 0.708f, 0.292f }));
	CHECK(complete.whitePoint == (std::array<float, 2> { 0.3127f, 0.3290f }));
	CHECK(complete.maxContentLightLevel == 0 && complete.maxLuminance == 1000);

	return failures == 0 ? 0 : 1;
}
