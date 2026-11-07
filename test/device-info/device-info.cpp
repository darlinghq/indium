// Copyright (C) 2026 Darling Developers
//
// The public device accessors agree with what Vulkan reports for the same physical device, and a
// preferred device can be selected by index or by part of its name.

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

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
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			return 0;
		}
		auto physicalDevice = std::static_pointer_cast<Indium::PrivateDevice>(device)->physicalDevice();

		VkPhysicalDeviceIDProperties idProperties {};
		idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
		VkPhysicalDeviceProperties2 properties {};
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &idProperties;
		Indium::DynamicVK::vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
		VkPhysicalDeviceMemoryProperties memory {};
		Indium::DynamicVK::vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);

		std::printf("     %s: vendor %04x device %04x type %d\n", device->name().c_str(), device->vendorID(), device->deviceID(), static_cast<int>(device->deviceType()));
		report(device->vendorID() == properties.properties.vendorID && device->deviceID() == properties.properties.deviceID, "vendor and device ID");
		report(properties.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU || device->deviceType() == Indium::DeviceType::CPU, "device type");

		auto uuid = device->deviceUUID();
		report(std::memcmp(uuid.data(), idProperties.deviceUUID, 16) == 0, "device UUID");

		auto heaps = device->memoryHeaps();
		bool heapsMatch = heaps.size() == memory.memoryHeapCount;
		for (size_t i = 0; heapsMatch && i < heaps.size(); ++i) {
			heapsMatch = heaps[i].size == memory.memoryHeaps[i].size &&
				heaps[i].deviceLocal == !!(memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);
		}
		report(heapsMatch, "memory heaps");

		const uint32_t allTypes = (1u << memory.memoryTypeCount) - 1;
		auto shared = device->memoryTypeIndex(allTypes, Indium::StorageMode::Shared);
		report(shared && (memory.memoryTypes[*shared].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "shared storage uses host-visible memory");
		report(!device->memoryTypeIndex(0, Indium::StorageMode::Private), "no memory type when none is allowed");

		auto all = Indium::copyAllDevices();
		report(!all.empty() && all.front() == device, "the default device comes first");
		report(Indium::createSystemDefaultDevice("0") == all.front() && Indium::createSystemDefaultDevice(std::to_string(all.size() - 1)) == all.back(), "a device chosen by index");

		auto name = device->name();
		std::string part = name.substr(name.size() / 3, std::max<size_t>(1, name.size() / 3));
		std::transform(part.begin(), part.end(), part.begin(), [](unsigned char c) { return std::toupper(c); });
		auto byName = Indium::createSystemDefaultDevice(part);
		report(byName && byName->name().size() && std::find(all.begin(), all.end(), byName) != all.end(), "a device chosen by part of its name");
		report(Indium::createSystemDefaultDevice("no such device, surely") == device && Indium::createSystemDefaultDevice("") == device && Indium::createSystemDefaultDevice("99999999999999999999999") == device, "an unmatched preference selects the default");
	}

	Indium::finit();

	return failures ? 1 : 0;
}
