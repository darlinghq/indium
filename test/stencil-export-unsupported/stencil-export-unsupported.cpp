// Copyright (C) 2026 Darling Developers
//
// A fragment function that writes the stencil reference ([[stencil]]) makes its module declare the StencilExportEXT
// capability, which a Vulkan device may only be given with VK_EXT_shader_stencil_export. On a device without it:
//  - the library still loads, and the module given to Vulkan declares no StencilExportEXT (nor the extension, the
//    FragStencilRefEXT built-in or the StencilRefReplacingEXT execution mode);
//  - a render pipeline with a fragment function of the library that does not write the stencil reference is made and
//    draws what it should;
//  - a render pipeline with the function that writes it is refused, rather than made of a function that cannot do
//    what it says.
// The device without the extension is made from the physical device of the system default device while the
// extension is left out of what vkEnumerateDeviceExtensionProperties lists. On a device with the extension the
// function that writes the stencil reference is accepted.
// The rewrite is also checked on two hand-written modules (see README.md): one that declares a private pointer type
// only after the stencil reference, which the rewritten variable may not use, and one the rewrite has to leave alone.
// Prints SKIP and exits 0 without a Vulkan device.

#include "stencil-export-unsupported-spv.h"
#include "late-private-pointer-spv.h"
#include "copied-pointer-spv.h"

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/library.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	std::fflush(stdout);
	failures += !ok;
}

constexpr uint32_t opCapability = 17, opExtension = 10, opExecutionMode = 16, opDecorate = 71;
constexpr uint32_t capabilityStencilExport = 5013, decorationBuiltIn = 11, builtInFragStencilRef = 5014, executionModeStencilRefReplacing = 5027;

// whether the module says anything that needs VK_EXT_shader_stencil_export
bool mentionsStencilExport(const std::vector<uint32_t>& words) {
	for (size_t at = 5; at < words.size();) {
		uint32_t count = words[at] >> 16;
		uint32_t opcode = words[at] & 0xffff;
		if (count == 0 || at + count > words.size()) {
			return true;
		}
		if ((opcode == opCapability && words[at + 1] == capabilityStencilExport)
			|| (opcode == opExtension && std::strcmp(reinterpret_cast<const char*>(&words[at + 1]), "SPV_EXT_shader_stencil_export") == 0)
			|| (opcode == opExecutionMode && count == 3 && words[at + 2] == executionModeStencilRefReplacing)
			|| (opcode == opDecorate && count == 4 && words[at + 2] == decorationBuiltIn && words[at + 3] == builtInFragStencilRef)) {
			return true;
		}
		at += count;
	}
	return false;
}

std::vector<uint32_t> moduleWords(const unsigned char* bytes, size_t length) {
	std::vector<uint32_t> words(length / sizeof(uint32_t));
	std::memcpy(words.data(), bytes, words.size() * sizeof(uint32_t));
	return words;
}

// whether every variable's type, and every type a pointer type points to, is declared before it, and whether any
// decoration is left on a variable that is not an input or output
bool declaredInOrder(const std::vector<uint32_t>& words) {
	constexpr uint32_t opTypeInt = 21, opTypePointer = 32, opVariable = 59;
	std::unordered_set<uint32_t> declared;
	std::unordered_set<uint32_t> privateVariables;
	for (size_t at = 5; at < words.size();) {
		uint32_t count = words[at] >> 16;
		uint32_t opcode = words[at] & 0xffff;
		if (count == 0 || at + count > words.size()) {
			return false;
		}
		if (opcode == opTypeInt) {
			declared.insert(words[at + 1]);
		} else if (opcode == opTypePointer && count == 4) {
			if (declared.count(words[at + 3]) == 0) {
				return false;
			}
			declared.insert(words[at + 1]);
		} else if (opcode == opVariable && count >= 4) {
			if (declared.count(words[at + 1]) == 0) {
				return false;
			}
			if (words[at + 3] == 6) {
				privateVariables.insert(words[at + 2]);
			}
		}
		at += count;
	}
	for (size_t at = 5; at < words.size();) {
		uint32_t count = words[at] >> 16;
		if ((words[at] & 0xffff) == opDecorate && privateVariables.count(words[at + 1]) != 0) {
			return false;
		}
		at += count;
	}
	return true;
}

PFN_vkEnumerateDeviceExtensionProperties originalEnumerate = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL enumerateWithoutStencilExport(VkPhysicalDevice physicalDevice, const char* layer, uint32_t* count, VkExtensionProperties* properties) {
	uint32_t all = 0;
	originalEnumerate(physicalDevice, layer, &all, nullptr);
	std::vector<VkExtensionProperties> extensions(all);
	originalEnumerate(physicalDevice, layer, &all, extensions.data());
	extensions.resize(all);
	std::vector<VkExtensionProperties> kept;
	for (const auto& extension: extensions) {
		if (std::strcmp(extension.extensionName, VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME) != 0) {
			kept.push_back(extension);
		}
	}
	if (!properties) {
		*count = kept.size();
		return VK_SUCCESS;
	}
	uint32_t written = std::min<uint32_t>(*count, kept.size());
	std::copy(kept.begin(), kept.begin() + written, properties);
	*count = written;
	return written < kept.size() ? VK_INCOMPLETE : VK_SUCCESS;
}

// the modules the device is given
std::vector<std::vector<uint32_t>> createdModules;
PFN_vkCreateShaderModule originalCreateShaderModule = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL recordShaderModule(VkDevice device, const VkShaderModuleCreateInfo* info, const VkAllocationCallbacks* allocator, VkShaderModule* module) {
	const auto* words = info->pCode;
	createdModules.emplace_back(words, words + info->codeSize / sizeof(uint32_t));
	return originalCreateShaderModule(device, info, allocator, module);
}

Indium::LibraryReflection libraryReflection() {
	Indium::LibraryReflection reflection;
	reflection.functions["fullscreen_vs"].functionType = Indium::FunctionType::Vertex;
	reflection.functions["color_fs"].functionType = Indium::FunctionType::Fragment;
	reflection.functions["stencil_fs"].functionType = Indium::FunctionType::Fragment;
	return reflection;
}

Indium::RenderPipelineDescriptor pipelineDescriptor(const std::shared_ptr<Indium::Library>& library, const char* fragment) {
	Indium::RenderPipelineDescriptor descriptor;
	descriptor.vertexFunction = library->newFunction("fullscreen_vs");
	descriptor.fragmentFunction = library->newFunction(fragment);
	descriptor.colorAttachments.emplace_back();
	descriptor.colorAttachments.back().pixelFormat = Indium::PixelFormat::RGBA8Unorm;
	return descriptor;
}

// whether a pipeline can be made of the vertex function and `fragment`; the error is put in `error`
std::shared_ptr<Indium::RenderPipelineState> tryPipeline(const std::shared_ptr<Indium::Device>& device, const std::shared_ptr<Indium::Library>& library, const char* fragment, std::string& error) {
	try {
		return device->newRenderPipelineState(pipelineDescriptor(library, fragment));
	} catch (const std::exception& exception) {
		error = exception.what();
		return nullptr;
	}
}

}

int main() {
	// a submission that never completes is a failure, not a hang
	std::thread([]() {
		std::this_thread::sleep_for(std::chrono::seconds(60));
		std::printf("FAIL timed out\n");
		std::fflush(stdout);
		_exit(1);
	}).detach();

	{
		auto words = moduleWords(stencil_export_unsupported_spv, stencil_export_unsupported_spv_len);
		std::unordered_set<std::string> writers;
		bool rewritten = Indium::spirvWithoutStencilExport(words, writers);
		report(rewritten && !mentionsStencilExport(words), "the module is rewritten without anything that needs VK_EXT_shader_stencil_export");
		report(writers == std::unordered_set<std::string> { "stencil_fs" }, "stencil_fs, and only it, is the function that writes the stencil reference");
		auto again = words;
		std::unordered_set<std::string> none;
		report(!Indium::spirvWithoutStencilExport(again, none) && again == words && none.empty(), "a module without the capability is left as it is");

		auto late = moduleWords(late_private_pointer_spv, late_private_pointer_spv_len);
		std::unordered_set<std::string> lateWriters;
		bool lateRewritten = Indium::spirvWithoutStencilExport(late, lateWriters);
		report(lateRewritten && !mentionsStencilExport(late) && declaredInOrder(late) && lateWriters == std::unordered_set<std::string> { "main" },
			"a private pointer type declared after the stencil reference is not used for it, and its Invariant decoration is removed");

		auto copied = moduleWords(copied_pointer_spv, copied_pointer_spv_len);
		auto copiedBefore = copied;
		std::unordered_set<std::string> copiedWriters;
		report(!Indium::spirvWithoutStencilExport(copied, copiedWriters) && copied == copiedBefore && copiedWriters.empty(),
			"a module that copies the stencil reference's pointer is left as it is");
	}

	Indium::init(nullptr, 0, false);

	{
		if (Indium::copyAllDevices().empty()) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}
		auto systemDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(Indium::createSystemDefaultDevice());
		if (!systemDevice || !Indium::DynamicVK::vkEnumerateDeviceExtensionProperties.resolve() || !Indium::DynamicVK::vkCreateShaderModule.resolve()) {
			return 3;
		}

		originalEnumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(Indium::DynamicVK::vkEnumerateDeviceExtensionProperties.pointer);
		Indium::DynamicVK::vkEnumerateDeviceExtensionProperties.pointer = reinterpret_cast<void*>(enumerateWithoutStencilExport);
		auto device = std::make_shared<Indium::PrivateDevice>(systemDevice->physicalDevice());
		Indium::DynamicVK::vkEnumerateDeviceExtensionProperties.pointer = reinterpret_cast<void*>(originalEnumerate);
		std::printf("device: %s\n", device->name().c_str());

		auto queue = device->newCommandQueue();
		bool keepPolling = true;
		std::thread poller([&]() {
			while (keepPolling) {
				device->pollEvents(UINT64_MAX);
			}
		});

		report(!(device->features() & Indium::PrivateDevice::Feature::ShaderStencilExport), "the device has no VK_EXT_shader_stencil_export");

		originalCreateShaderModule = reinterpret_cast<PFN_vkCreateShaderModule>(Indium::DynamicVK::vkCreateShaderModule.pointer);
		Indium::DynamicVK::vkCreateShaderModule.pointer = reinterpret_cast<void*>(recordShaderModule);
		std::string error;
		auto library = device->newLibrary(stencil_export_unsupported_spv, stencil_export_unsupported_spv_len, libraryReflection(), &error);
		std::string colorError;
		auto colorPipeline = library ? tryPipeline(device, library, "color_fs", colorError) : nullptr;
		Indium::DynamicVK::vkCreateShaderModule.pointer = reinterpret_cast<void*>(originalCreateShaderModule);

		report(library != nullptr, "the library loads" + (error.empty() ? std::string() : ": " + error));
		bool clean = !createdModules.empty();
		for (const auto& module: createdModules) {
			clean = clean && !mentionsStencilExport(module);
		}
		report(clean, "the modules given to the device declare nothing that needs VK_EXT_shader_stencil_export");
		report(colorPipeline != nullptr, "a pipeline of a function that does not write the stencil reference is made" + (colorError.empty() ? std::string() : ": " + colorError));

		if (colorPipeline) {
			auto targetDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
			targetDescriptor.usage = Indium::TextureUsage::RenderTarget;
			auto target = device->newTexture(targetDescriptor);
			Indium::RenderPassDescriptor pass {};
			pass.colorAttachments.emplace_back();
			pass.colorAttachments.back().texture = target;
			pass.colorAttachments.back().loadAction = Indium::LoadAction::Clear;
			pass.colorAttachments.back().storeAction = Indium::StoreAction::Store;
			pass.colorAttachments.back().clearColor = Indium::ClearColor(0, 0, 0, 0);

			auto commandBuffer = queue->commandBuffer();
			auto render = commandBuffer->renderCommandEncoder(pass);
			render->setRenderPipelineState(colorPipeline);
			render->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3, 1);
			render->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();

			std::vector<uint32_t> texels(16, 0);
			target->getBytes(Indium::Region::make2D(0, 0, 4, 4), 0, texels.data(), 4 * 4);
			// color_fs returns (0.2, 0.4, 0.6, 0.8): 51, 102, 153 and 204 as unorm8
			bool drawn = true;
			for (auto texel: texels) {
				drawn = drawn && texel == 0xcc996633u;
			}
			char got[64];
			std::snprintf(got, sizeof(got), "%08x", texels[0]);
			report(drawn, std::string("it draws the color of color_fs, got ") + got);
		}

		if (library) {
			std::string stencilError;
			auto stencilPipeline = tryPipeline(device, library, "stencil_fs", stencilError);
			report(stencilPipeline == nullptr && stencilError.find("stencil reference") != std::string::npos, "a pipeline of the function that writes the stencil reference is refused" + (stencilError.empty() ? std::string() : ": " + stencilError));
		}

		keepPolling = false;
		device->wakeupEventLoop();
		poller.join();

		if (!!(systemDevice->features() & Indium::PrivateDevice::Feature::ShaderStencilExport)) {
			auto systemLibrary = systemDevice->newLibrary(stencil_export_unsupported_spv, stencil_export_unsupported_spv_len, libraryReflection(), &error);
			std::string stencilError;
			auto stencilPipeline = systemLibrary ? tryPipeline(systemDevice, systemLibrary, "stencil_fs", stencilError) : nullptr;
			report(stencilPipeline != nullptr, "on a device with VK_EXT_shader_stencil_export the function that writes the stencil reference is accepted" + (stencilError.empty() ? std::string() : ": " + stencilError));
		} else {
			std::printf("SKIP the system default device has no VK_EXT_shader_stencil_export\n");
		}
	}

	Indium::finit();
	return failures ? 1 : 0;
}
