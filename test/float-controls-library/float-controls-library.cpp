// Copyright (C) 2026 Darling Developers
//
// A module whose entry points carry the SignedZeroInfNanPreserve execution mode (as mslc's do) needs the device to
// report shaderSignedZeroInfNanPreserveFloat<width> for the width the mode names, or Vulkan refuses the pipeline.
// newLibrary(spirv, ...) refuses such a library up front, returning nullptr and a message:
//  - the module's own execution modes decide, so a module without the mode is made on a device that reports no
//    width, and so is a library translated from AIR;
//  - the message names every width the device lacks and none it has;
//  - a device that reports none of the widths is still listed (it is not dropped when the devices are enumerated);
//  - a device that reports the widths makes the library, and a pipeline of it.
// No machine to test on lacks the widths, so the devices that lack some are made while the property query is
// replaced with one that clears the widths a mask names. Prints SKIP and exits 0 without a Vulkan device.

#include "float32-spv.h"
#include "float16-32-64-spv.h"
#include "plain-spv.h"
#include "air-add.h"

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/library.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	std::fflush(stdout);
	failures += !ok;
}

// the widths the replaced property query leaves in place, as SignedZeroInfNanPreserveWidth bits
uint32_t keptWidths = 0xffffffffu;
PFN_vkGetPhysicalDeviceProperties2 originalProperties2 = nullptr;

VKAPI_ATTR void VKAPI_CALL clearWidths(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* properties) {
	originalProperties2(physicalDevice, properties);
	for (auto* link = static_cast<VkBaseOutStructure*>(properties->pNext); link; link = link->pNext) {
		if (link->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES) {
			auto* controls = reinterpret_cast<VkPhysicalDeviceFloatControlsProperties*>(link);
			controls->shaderSignedZeroInfNanPreserveFloat16 &= !!(keptWidths & Indium::SignedZeroInfNanPreserve16);
			controls->shaderSignedZeroInfNanPreserveFloat32 &= !!(keptWidths & Indium::SignedZeroInfNanPreserve32);
			controls->shaderSignedZeroInfNanPreserveFloat64 &= !!(keptWidths & Indium::SignedZeroInfNanPreserve64);
		}
	}
}

std::shared_ptr<Indium::PrivateDevice> deviceReporting(const std::shared_ptr<Indium::PrivateDevice>& system, uint32_t widths) {
	keptWidths = widths;
	Indium::DynamicVK::vkGetPhysicalDeviceProperties2.pointer = reinterpret_cast<void*>(clearWidths);
	auto device = std::make_shared<Indium::PrivateDevice>(system->physicalDevice());
	Indium::DynamicVK::vkGetPhysicalDeviceProperties2.pointer = reinterpret_cast<void*>(originalProperties2);
	return device;
}

Indium::LibraryReflection kernels(std::initializer_list<const char*> names) {
	Indium::LibraryReflection reflection;
	for (auto name: names) {
		reflection.functions[name].functionType = Indium::FunctionType::Kernel;
	}
	return reflection;
}

struct Result {
	std::shared_ptr<Indium::Library> library;
	std::string error;
};

Result make(const std::shared_ptr<Indium::PrivateDevice>& device, const unsigned char* module, size_t length, const Indium::LibraryReflection& reflection) {
	Result result;
	result.error = "unset";
	result.library = device->newLibrary(module, length, reflection, &result.error);
	return result;
}

bool mentions(const std::string& text, const char* width) {
	return text.find(std::string("shaderSignedZeroInfNanPreserveFloat") + width) != std::string::npos;
}

}

// the devices are released before Indium::finit() unloads Vulkan
static int run() {
	if (Indium::copyAllDevices().empty()) {
		std::printf("SKIP no device\n");
		return 0;
	}

	auto system = std::dynamic_pointer_cast<Indium::PrivateDevice>(Indium::createSystemDefaultDevice());
	if (!system || !Indium::DynamicVK::vkGetPhysicalDeviceProperties2.resolve()) {
		return 3;
	}
	originalProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(Indium::DynamicVK::vkGetPhysicalDeviceProperties2.pointer);
	std::printf("device: %s\n", system->name().c_str());

	const auto onlyFloat32 = deviceReporting(system, Indium::SignedZeroInfNanPreserve32);
	const auto nothing = deviceReporting(system, 0);
	const auto everything = deviceReporting(system, 0xffffffffu);
	const auto half32 = kernels({ "half_main", "float_main", "double_main" });

	{
		auto r = make(nothing, float32_spv, float32_spv_len, kernels({ "main" }));
		report(r.library == nullptr, "a module that wants 32-bit float controls is refused on a device that reports no width");
		report(mentions(r.error, "32") && !mentions(r.error, "16") && !mentions(r.error, "64"), "the message names the 32-bit width, and no other: " + r.error);

		auto none = nothing->newLibrary(float32_spv, float32_spv_len, kernels({ "main" }));
		report(none == nullptr, "the refusal does not need a message pointer");
	}

	{
		auto r = make(nothing, float16_32_64_spv, float16_32_64_spv_len, half32);
		report(r.library == nullptr && mentions(r.error, "16") && mentions(r.error, "32") && mentions(r.error, "64"), "a module that wants all three widths is refused, naming all of them: " + r.error);
	}

	{
		auto r = make(onlyFloat32, float32_spv, float32_spv_len, kernels({ "main" }));
		report(r.library != nullptr && r.error.empty(), "the 32-bit module is made on a device that reports the 32-bit width, with an empty message");
	}

	{
		auto r = make(onlyFloat32, float16_32_64_spv, float16_32_64_spv_len, half32);
		report(r.library == nullptr && mentions(r.error, "16") && !mentions(r.error, "32") && mentions(r.error, "64"), "the module that wants all three widths names only the missing ones: " + r.error);
	}

	{
		auto r = make(everything, float16_32_64_spv, float16_32_64_spv_len, half32);
		report(r.library != nullptr && r.error.empty(), "a device that reports all widths makes the library of all three");
	}

	{
		auto r = make(nothing, plain_spv, plain_spv_len, kernels({ "main" }));
		report(r.library != nullptr && r.error.empty(), "a module with no float control is made on a device that reports no width");
	}

	{
		// the scan reads only what the module holds: an instruction whose length runs past the end, or is 0, ends it
		std::vector<uint32_t> words(float16_32_64_spv_len / sizeof(uint32_t));
		std::memcpy(words.data(), float16_32_64_spv, float16_32_64_spv_len);
		const uint32_t all = Indium::SignedZeroInfNanPreserve16 | Indium::SignedZeroInfNanPreserve32 | Indium::SignedZeroInfNanPreserve64;
		report(Indium::spirvSignedZeroInfNanPreserveWidths(words.data(), words.size()) == all, "the scan finds the widths of all three modes");
		report(Indium::spirvSignedZeroInfNanPreserveWidths(words.data(), 5) == 0 && Indium::spirvSignedZeroInfNanPreserveWidths(nullptr, 0) == 0, "a header alone, or nothing, asks for no width");

		size_t at = 5;
		while (at < words.size() && (words[at] & 0xffff) != 16) {
			at += words[at] >> 16;
		}
		auto cut = words;
		report(Indium::spirvSignedZeroInfNanPreserveWidths(cut.data(), at + 3) == 0, "an execution mode cut short is not read");
		cut[at] = (cut[at] & 0xffff) | (200u << 16);
		report(Indium::spirvSignedZeroInfNanPreserveWidths(cut.data(), cut.size()) == 0, "an instruction longer than the module ends the scan");
		cut[at] &= 0xffff;
		report(Indium::spirvSignedZeroInfNanPreserveWidths(cut.data(), cut.size()) == 0, "an instruction of length 0 ends the scan");
	}

	{
		// a library translated from AIR is not judged by the check, whatever the device reports
		auto library = nothing->newLibrary(air_add, air_add_len);
		report(library != nullptr && library->newFunction("add_arrays") != nullptr, "a library translated from AIR is made on a device that reports no width");
	}

	// what the real device reports
	uint32_t real = 0;
	{
		VkPhysicalDeviceFloatControlsProperties controls {};
		controls.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES;
		VkPhysicalDeviceProperties2 properties {};
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &controls;
		originalProperties2(system->physicalDevice(), &properties);
		real = (controls.shaderSignedZeroInfNanPreserveFloat16 ? Indium::SignedZeroInfNanPreserve16 : 0)
			| (controls.shaderSignedZeroInfNanPreserveFloat32 ? Indium::SignedZeroInfNanPreserve32 : 0)
			| (controls.shaderSignedZeroInfNanPreserveFloat64 ? Indium::SignedZeroInfNanPreserve64 : 0);
	}

	{
		// a real device that reports the 32-bit width makes the library and a pipeline of it
		if (real & Indium::SignedZeroInfNanPreserve32) {
			auto r = make(system, float32_spv, float32_spv_len, kernels({ "main" }));
			std::shared_ptr<Indium::ComputePipelineState> pipeline;
			std::string pipelineError;
			if (r.library) {
				try {
					pipeline = system->newComputePipelineState(r.library->newFunction("main"));
				} catch (const std::exception& exception) {
					pipelineError = exception.what();
				}
			}
			report(r.library != nullptr && pipeline != nullptr, "the real device makes the 32-bit module's library and a compute pipeline of it " + r.error + pipelineError);
		} else {
			std::printf("SKIP the system default device does not report the 32-bit width\n");
			report(make(system, float32_spv, float32_spv_len, kernels({ "main" })).library == nullptr, "the real device refuses the 32-bit module");
		}
	}

	{
		// the devices are listed again while the query reports no width: none is dropped for lacking one
		const size_t listed = Indium::globalDeviceList.size();
		Indium::finitGlobalDeviceList();
		keptWidths = 0;
		Indium::DynamicVK::vkGetPhysicalDeviceProperties2.pointer = reinterpret_cast<void*>(clearWidths);
		Indium::initGlobalDeviceList();
		Indium::DynamicVK::vkGetPhysicalDeviceProperties2.pointer = reinterpret_cast<void*>(originalProperties2);
		report(Indium::globalDeviceList.size() == listed && listed != 0, "devices that report no float control width are still listed");
	}

	return failures ? 1 : 0;
}


int main() {
	Indium::init(nullptr, 0, false);
	int result = run();
	Indium::finit();
	return result;
}
