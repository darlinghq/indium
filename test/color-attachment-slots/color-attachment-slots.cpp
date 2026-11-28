// Copyright (C) 2026 Darling Developers
//
// A render pass descriptor has a slot for each color attachment, and a render pipeline descriptor a pixel format
// for each; slots after the last one used are empty (Metal's descriptors have eight). Vulkan requires a pipeline's
// color blend state to have exactly as many attachments as the subpass of the render pass it is created for. This
// creates pipelines and passes whose descriptors have different numbers of trailing empty slots and checks that:
//  - each pipeline's color blend state has as many attachments as the render pass it is created for;
//  - a pipeline with one color format draws in a pass with eight slots, the first one with a texture.
// The shaders are test/fill-mode-depth-clip's. Prints SKIP and exits 0 without a Vulkan device.

#include "graphics-spv.h"

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstdint>
#include <cstdio>
#include <map>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t size = 16;

PFN_vkCreateRenderPass originalCreateRenderPass;
PFN_vkCreateGraphicsPipelines originalCreateGraphicsPipelines;
std::map<VkRenderPass, uint32_t> subpassColorCounts;
unsigned pipelinesCreated = 0;
unsigned pipelinesMismatched = 0;

VkResult VKAPI_CALL recordRenderPass(VkDevice device, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks* allocator, VkRenderPass* renderPass) {
	auto result = originalCreateRenderPass(device, info, allocator, renderPass);
	if (result == VK_SUCCESS && info->subpassCount > 0) {
		subpassColorCounts[*renderPass] = info->pSubpasses[0].colorAttachmentCount;
	}
	return result;
}

VkResult VKAPI_CALL recordPipelines(VkDevice device, VkPipelineCache cache, uint32_t count, const VkGraphicsPipelineCreateInfo* infos, const VkAllocationCallbacks* allocator, VkPipeline* pipelines) {
	for (uint32_t i = 0; i < count; ++i) {
		++pipelinesCreated;
		auto found = subpassColorCounts.find(infos[i].renderPass);
		uint32_t blendCount = infos[i].pColorBlendState ? infos[i].pColorBlendState->attachmentCount : 0;
		if (found == subpassColorCounts.end() || found->second != blendCount) {
			++pipelinesMismatched;
		}
	}
	return originalCreateGraphicsPipelines(device, cache, count, infos, allocator, pipelines);
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
		if (!Indium::DynamicVK::vkCreateRenderPass.resolve() || !Indium::DynamicVK::vkCreateGraphicsPipelines.resolve()) {
			return 3;
		}
		originalCreateRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(Indium::DynamicVK::vkCreateRenderPass.pointer);
		Indium::DynamicVK::vkCreateRenderPass.pointer = reinterpret_cast<void*>(recordRenderPass);
		originalCreateGraphicsPipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(Indium::DynamicVK::vkCreateGraphicsPipelines.pointer);
		Indium::DynamicVK::vkCreateGraphicsPipelines.pointer = reinterpret_cast<void*>(recordPipelines);

		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});
		auto queue = device->newCommandQueue();

		Indium::LibraryReflection reflection;
		reflection.functions["smallVertex"].functionType = Indium::FunctionType::Vertex;
		reflection.functions["redFragment"].functionType = Indium::FunctionType::Fragment;
		std::string error;
		auto library = device->newLibrary(graphics_spv, graphics_spv_len, reflection, &error);
		report(library != nullptr, "the module loads");
		if (library) {
			auto textureDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, size, size, false);
			textureDescriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;

			// a pipeline whose descriptor has `formatSlots` slots, with a format in `formatSlot` only, draws in a
			// pass whose descriptor has `passSlots` slots, with a texture in `textureSlot` only
			auto run = [&](size_t formatSlots, size_t formatSlot, size_t passSlots, size_t textureSlot) {
				Indium::RenderPipelineDescriptor descriptor {};
				descriptor.vertexFunction = library->newFunction("smallVertex");
				descriptor.fragmentFunction = library->newFunction("redFragment");
				descriptor.colorAttachments.resize(formatSlots);
				for (auto& attachment: descriptor.colorAttachments) {
					attachment.pixelFormat = Indium::PixelFormat::Invalid;
				}
				descriptor.colorAttachments[formatSlot].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
				auto pipeline = device->newRenderPipelineState(descriptor);

				auto target = device->newTexture(textureDescriptor);
				Indium::RenderPassDescriptor pass {};
				pass.colorAttachments.resize(passSlots);
				pass.colorAttachments[textureSlot].texture = target;
				pass.colorAttachments[textureSlot].loadAction = Indium::LoadAction::Clear;
				pass.colorAttachments[textureSlot].storeAction = Indium::StoreAction::Store;
				pass.colorAttachments[textureSlot].clearColor = Indium::ClearColor(0, 0, 0, 0);
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->renderCommandEncoder(pass);
				encoder->setRenderPipelineState(pipeline);
				encoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				std::vector<uint32_t> pixels(size * size);
				target->getBytes(Indium::Region::make2D(0, 0, size, size), 0, pixels.data(), size * 4);
				return pixels[(size / 2 - 1) * size + size / 2];
			};

			pipelinesCreated = pipelinesMismatched = 0;
			uint32_t drawn = run(1, 0, 8, 0);
			report(pipelinesCreated > 0 && pipelinesMismatched == 0, "a pipeline with one slot, in a pass with eight, has as many blend attachments as the subpass");
			report(drawn == 0xFF0000FFu, "a pipeline with one slot draws in a pass with eight slots");

			pipelinesCreated = pipelinesMismatched = 0;
			run(8, 0, 1, 0);
			report(pipelinesCreated > 0 && pipelinesMismatched == 0, "a pipeline with eight slots, in a pass with one, has as many blend attachments as the subpass");

			// the fragment shader writes color 0, which has no texture here; only the pipeline is checked
			pipelinesCreated = pipelinesMismatched = 0;
			run(8, 1, 8, 1);
			report(pipelinesCreated > 0 && pipelinesMismatched == 0, "with a gap before the only texture, the pipeline has as many blend attachments as the subpass");
		}

		Indium::DynamicVK::vkCreateRenderPass.pointer = reinterpret_cast<void*>(originalCreateRenderPass);
		Indium::DynamicVK::vkCreateGraphicsPipelines.pointer = reinterpret_cast<void*>(originalCreateGraphicsPipelines);

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
