// Copyright (C) 2026 Lubos Dolezel
//
// The order Indium offers devices in, on synthetic device lists: GPUs before anything else, the
// discrete GPU first, the loader's order kept among devices of the same kind, and CPU rasterizers
// only when there is no GPU.

#include <indium/device-order.private.hpp>

#include <cstdio>
#include <string>

namespace {

constexpr auto cpu = VK_PHYSICAL_DEVICE_TYPE_CPU;
constexpr auto discrete = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
constexpr auto integrated = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
constexpr auto virtualGPU = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
constexpr auto other = VK_PHYSICAL_DEVICE_TYPE_OTHER;

unsigned failures = 0;

std::string join(const std::vector<size_t>& order) {
	std::string result;
	for (size_t index: order) {
		if (!result.empty()) {
			result += ' ';
		}
		result += std::to_string(index);
	}
	return result;
}

void expect(const char* what, const std::vector<VkPhysicalDeviceType>& types, const std::vector<size_t>& want) {
	auto got = Indium::orderDevices(types);
	bool ok = got == want;
	std::printf("%s %s: [%s] (expected [%s])\n", ok ? "PASS" : "FAIL", what, join(got).c_str(), join(want).c_str());
	failures += !ok;
}

}

int main() {
	expect("no devices", {}, {});
	expect("a CPU rasterizer alone is offered", { cpu }, { 0 });
	expect("two CPU rasterizers keep the loader's order", { cpu, cpu }, { 0, 1 });
	expect("a CPU rasterizer enumerated first is left out", { cpu, integrated }, { 1 });
	expect("a CPU rasterizer enumerated last is left out", { discrete, cpu }, { 0 });
	expect("a virtual GPU is a GPU", { cpu, virtualGPU }, { 1 });
	expect("the discrete GPU comes first", { integrated, discrete }, { 1, 0 });
	expect("GPUs of one kind keep the loader's order", { discrete, integrated, discrete }, { 0, 2, 1 });
	expect("all kinds", { cpu, other, virtualGPU, integrated, discrete }, { 4, 3, 2, 1 });
	expect("an unknown device type is offered after the GPUs", { other, integrated }, { 1, 0 });
	expect("an unknown device type without a GPU", { cpu, other }, { 0, 1 });

	return failures ? 1 : 0;
}
