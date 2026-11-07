// Copyright (C) 2026 Darling Developers
//
// The memory type each storage mode is allocated from, on memory type layouts that Vulkan's
// ordering rule allows: a type whose properties are a subset of another's comes first.

#include <indium/memory-type.private.hpp>

#include <cstdio>

namespace {

constexpr VkMemoryPropertyFlags local = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
constexpr VkMemoryPropertyFlags visible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
constexpr VkMemoryPropertyFlags coherent = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
constexpr VkMemoryPropertyFlags cached = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

VkPhysicalDeviceMemoryProperties layout(std::initializer_list<VkMemoryPropertyFlags> types) {
	VkPhysicalDeviceMemoryProperties properties {};
	for (auto flags: types) {
		properties.memoryTypes[properties.memoryTypeCount++].propertyFlags = flags;
	}
	return properties;
}

unsigned failures = 0;

void expect(const char* what, size_t got, size_t want) {
	bool ok = got == want;
	std::printf("%s %s: %zd (expected %zd)\n", ok ? "PASS" : "FAIL", what, static_cast<ssize_t>(got), static_cast<ssize_t>(want));
	failures += !ok;
}

}

int main() {
	using Indium::StorageMode;
	using Indium::chooseMemoryType;

	// A discrete GPU that lists system memory without any property first, then device memory, then
	// host memory, then BAR memory.
	auto discrete = layout({ 0, local, visible | coherent, visible | coherent | cached, local | visible | coherent });
	expect("discrete, private", chooseMemoryType(discrete, 0x1f, StorageMode::Private), 1);
	expect("discrete, memoryless", chooseMemoryType(discrete, 0x1f, StorageMode::Memoryless), 1);
	expect("discrete, shared", chooseMemoryType(discrete, 0x1f, StorageMode::Shared), 2);
	expect("discrete, managed", chooseMemoryType(discrete, 0x1f, StorageMode::Managed), 2);
	// a resource that cannot live in device memory still gets the first type it may use
	expect("discrete, private without device memory", chooseMemoryType(discrete, 0x01, StorageMode::Private), 0);
	// Managed does not need coherent memory, Shared does
	auto incoherent = layout({ local, visible | cached });
	expect("incoherent, managed", chooseMemoryType(incoherent, 0x3, StorageMode::Managed), 1);
	expect("incoherent, shared", chooseMemoryType(incoherent, 0x3, StorageMode::Shared), static_cast<size_t>(SIZE_MAX));

	// A unified-memory GPU where the first type is device local and host visible.
	auto unified = layout({ local | visible | coherent, local | visible | coherent | cached });
	expect("unified, private", chooseMemoryType(unified, 0x3, StorageMode::Private), 0);
	expect("unified, shared", chooseMemoryType(unified, 0x3, StorageMode::Shared), 0);

	return failures ? 1 : 0;
}
