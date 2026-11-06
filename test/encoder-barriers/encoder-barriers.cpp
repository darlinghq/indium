// Copyright (C) 2026 Darling Developers
//
// Metal orders memory accesses between the encoders of one command buffer: what one encoder
// writes, the next one reads. Vulkan does not, so each encoder must end with a memory dependency.
// This records which pipeline barriers each encoder leaves behind when it ends.

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstdint>
#include <cstdio>
#include <thread>

namespace {

PFN_vkCmdPipelineBarrier original;
unsigned globalBarriers;

void VKAPI_CALL recordBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStages, VkPipelineStageFlags dstStages, VkDependencyFlags flags,
	uint32_t memoryBarrierCount, const VkMemoryBarrier* memoryBarriers,
	uint32_t bufferBarrierCount, const VkBufferMemoryBarrier* bufferBarriers,
	uint32_t imageBarrierCount, const VkImageMemoryBarrier* imageBarriers)
{
	for (uint32_t i = 0; i < memoryBarrierCount; ++i) {
		if ((memoryBarriers[i].srcAccessMask & VK_ACCESS_MEMORY_WRITE_BIT) && (memoryBarriers[i].dstAccessMask & VK_ACCESS_MEMORY_READ_BIT) &&
			(srcStages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) && (dstStages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)) {
			++globalBarriers;
		}
	}
	original(commandBuffer, srcStages, dstStages, flags, memoryBarrierCount, memoryBarriers, bufferBarrierCount, bufferBarriers, imageBarrierCount, imageBarriers);
}

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
		if (!Indium::DynamicVK::vkCmdPipelineBarrier.resolve()) {
			return 3;
		}

		bool keepPollingDevice = true;
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		original = reinterpret_cast<PFN_vkCmdPipelineBarrier>(Indium::DynamicVK::vkCmdPipelineBarrier.pointer.load());
		Indium::DynamicVK::vkCmdPipelineBarrier.pointer = reinterpret_cast<void*>(recordBarrier);

		auto queue = device->newCommandQueue();
		auto source = device->newBuffer(256, Indium::ResourceOptions::StorageModeShared);
		auto destination = device->newBuffer(256, Indium::ResourceOptions::StorageModeShared);
		auto commandBuffer = queue->commandBuffer();

		globalBarriers = 0;
		auto blit = commandBuffer->blitCommandEncoder();
		blit->copy(source, 0, destination, 0, 256);
		blit->endEncoding();
		report(globalBarriers == 1, "a blit encoder ends with a memory dependency");

		globalBarriers = 0;
		auto compute = commandBuffer->computeCommandEncoder();
		compute->endEncoding();
		report(globalBarriers == 1, "a compute encoder ends with a memory dependency");

		auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
		descriptor.usage = Indium::TextureUsage::RenderTarget;
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.emplace_back();
		pass.colorAttachments.back().texture = device->newTexture(descriptor);
		pass.colorAttachments.back().loadAction = Indium::LoadAction::Clear;
		globalBarriers = 0;
		auto render = commandBuffer->renderCommandEncoder(pass);
		render->endEncoding();
		report(globalBarriers == 1, "a render encoder ends with a memory dependency");

		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		Indium::DynamicVK::vkCmdPipelineBarrier.pointer = reinterpret_cast<void*>(original);

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
