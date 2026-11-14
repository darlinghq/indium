// Copyright (C) 2026 Lubos Dolezel
//
// Device::supportsPixelFormatSampling() answers from the Vulkan format properties of the format the
// pixel format maps to: sampling needs VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, filtered sampling also
// the linear filter bit, and a format with no Vulkan equivalent is not supported. The formats
// Vulkan makes mandatory for sampling and filtering must be reported, and the answers for the BC
// family agree with each other (a device either has textureCompressionBC or it does not).
// Prints SKIP and exits 0 without a Vulkan device.

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <indium/types.private.hpp>

#include <cstdio>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

}

int main() {
	Indium::init(nullptr, 0, false);

	{
		auto all = Indium::copyAllDevices();
		if (all.empty()) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}

		auto device = std::static_pointer_cast<Indium::PrivateDevice>(all.front());
		using Indium::PixelFormat;

		report(!device->supportsPixelFormatSampling(PixelFormat::Invalid, false), "Invalid cannot be sampled");
		report(!device->supportsPixelFormatSampling(PixelFormat::Invalid, true), "Invalid cannot be filtered");
		report(device->supportsPixelFormatSampling(PixelFormat::RGBA8Unorm, true), "RGBA8Unorm is sampled with filtering");
		report(device->supportsPixelFormatSampling(PixelFormat::BGRA8Unorm, true), "BGRA8Unorm is sampled with filtering");
		report(device->supportsPixelFormatSampling(PixelFormat::R8Unorm, true), "R8Unorm is sampled with filtering");

		// against the driver's own answers
		const std::vector<PixelFormat> formats {
			PixelFormat::R32Float, PixelFormat::RG32Float, PixelFormat::RGBA32Float,
			PixelFormat::RGBA16Float, PixelFormat::R16Float, PixelFormat::RGBA8Unorm_sRGB,
			PixelFormat::BC1_RGBA, PixelFormat::BC3_RGBA, PixelFormat::BC5_RGUnorm, PixelFormat::BC7_RGBAUnorm,
		};
		bool agrees = true;
		for (auto format: formats) {
			VkFormatProperties properties {};
			Indium::DynamicVK::vkGetPhysicalDeviceFormatProperties(device->physicalDevice(), Indium::pixelFormatToVkFormat(format), &properties);
			auto features = properties.optimalTilingFeatures;
			bool sampled = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
			bool filtered = sampled && (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
			agrees = agrees && device->supportsPixelFormatSampling(format, false) == sampled
				&& device->supportsPixelFormatSampling(format, true) == filtered;
		}
		report(agrees, "answers equal the Vulkan format features");

		bool consistent = true;
		for (auto format: formats) {
			consistent = consistent && (!device->supportsPixelFormatSampling(format, true) || device->supportsPixelFormatSampling(format, false));
		}
		report(consistent, "filtered sampling implies sampling");

		bool bc = device->supportsPixelFormatSampling(PixelFormat::BC1_RGBA, false);
		report(device->supportsPixelFormatSampling(PixelFormat::BC3_RGBA, false) == bc
			&& device->supportsPixelFormatSampling(PixelFormat::BC5_RGUnorm, false) == bc
			&& device->supportsPixelFormatSampling(PixelFormat::BC7_RGBAUnorm, false) == bc
			&& device->supportsPixelFormatSampling(PixelFormat::BC6H_RGBUfloat, false) == bc, "the BC formats are all supported or none");
	}

	Indium::finit();
	return failures ? 1 : 0;
}
