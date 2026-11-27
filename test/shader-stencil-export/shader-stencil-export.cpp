// Copyright (C) 2026 Darling Developers
//
// A fragment function that writes the stencil reference ([[stencil]]) is SPIR-V with the StencilExportEXT
// capability, and a shader module may declare it only on a device created with VK_EXT_shader_stencil_export. This
// checks that the Vulkan device is created with that extension when the physical device has it, and without it
// otherwise. Prints SKIP and exits 0 without a Vulkan device.

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

PFN_vkCreateDevice originalCreateDevice;
bool deviceCreated = false;
bool extensionEnabled = false;

VkResult VKAPI_CALL recordDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator, VkDevice* device) {
	deviceCreated = true;
	for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
		if (std::strcmp(info->ppEnabledExtensionNames[i], VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME) == 0) {
			extensionEnabled = true;
		}
	}
	return originalCreateDevice(physicalDevice, info, allocator, device);
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

		auto device = Indium::createSystemDefaultDevice();
		auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(device);
		if (!privateDevice || !Indium::DynamicVK::vkCreateDevice.resolve() || !Indium::DynamicVK::vkEnumerateDeviceExtensionProperties.resolve()) {
			return 3;
		}

		uint32_t count = 0;
		Indium::DynamicVK::vkEnumerateDeviceExtensionProperties(privateDevice->physicalDevice(), nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> extensions(count);
		Indium::DynamicVK::vkEnumerateDeviceExtensionProperties(privateDevice->physicalDevice(), nullptr, &count, extensions.data());
		bool offered = false;
		for (const auto& extension: extensions) {
			offered = offered || std::strcmp(extension.extensionName, VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME) == 0;
		}
		std::printf("device: %s, %s VK_EXT_shader_stencil_export\n", device->name().c_str(), offered ? "has" : "does not have");

		originalCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(Indium::DynamicVK::vkCreateDevice.pointer);
		Indium::DynamicVK::vkCreateDevice.pointer = reinterpret_cast<void*>(recordDevice);

		// the first queue creates the Vulkan device
		auto queue = device->newCommandQueue();
		Indium::DynamicVK::vkCreateDevice.pointer = reinterpret_cast<void*>(originalCreateDevice);

		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		report(deviceCreated, "the first queue creates the Vulkan device");
		report(extensionEnabled == offered, offered ? "the device is created with VK_EXT_shader_stencil_export, which the physical device has"
			: "the device is created without VK_EXT_shader_stencil_export, which the physical device does not have");

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
