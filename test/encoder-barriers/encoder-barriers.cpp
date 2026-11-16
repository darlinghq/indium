// Copyright (C) 2026 Darling Developers
//
// Metal orders memory accesses between the encoders of one command buffer: what one encoder
// writes, the next one reads. Vulkan does not. Indium orders them by what the encoders access (see
// test/hazard-tracking), so an encoder that accesses nothing ends without a memory dependency on everything.
// The exception is a command buffer that holds a texture kept equal to a buffer by copies around the encoders
// (Buffer::newTexture() at a row pitch the device's images cannot have): every encoder of those ends with a
// memory dependency on everything recorded before it. This records which barriers each encoder leaves behind
// when it ends, in both cases.

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstdint>
#include <cstdio>
#include <thread>

namespace {

PFN_vkCmdPipelineBarrier2 original;
unsigned globalBarriers;

// barriers from all commands to all commands that make every earlier write visible to every later access
void VKAPI_CALL recordBarrier(VkCommandBuffer commandBuffer, const VkDependencyInfo* info) {
	for (uint32_t i = 0; i < info->memoryBarrierCount; ++i) {
		const auto& barrier = info->pMemoryBarriers[i];
		if ((barrier.srcAccessMask & VK_ACCESS_2_MEMORY_WRITE_BIT) && (barrier.dstAccessMask & VK_ACCESS_2_MEMORY_READ_BIT) &&
			(barrier.srcStageMask & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) && (barrier.dstStageMask & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)) {
			++globalBarriers;
		}
	}
	original(commandBuffer, info);
}

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

std::shared_ptr<Indium::RenderPassDescriptor> renderPass(const std::shared_ptr<Indium::Device>& device) {
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
	descriptor.usage = Indium::TextureUsage::RenderTarget;
	auto pass = std::make_shared<Indium::RenderPassDescriptor>();
	pass->colorAttachments.emplace_back();
	pass->colorAttachments.back().texture = device->newTexture(descriptor);
	pass->colorAttachments.back().loadAction = Indium::LoadAction::Clear;
	return pass;
}

// the global barriers that recording an encoder of each kind leaves behind
void encodeEachKind(const std::shared_ptr<Indium::Device>& device, const std::shared_ptr<Indium::CommandQueue>& queue, unsigned& blitBarriers, unsigned& computeBarriers, unsigned& renderBarriers) {
	auto source = device->newBuffer(256, Indium::ResourceOptions::StorageModeShared);
	auto destination = device->newBuffer(256, Indium::ResourceOptions::StorageModeShared);
	auto commandBuffer = queue->commandBuffer();

	globalBarriers = 0;
	auto blit = commandBuffer->blitCommandEncoder();
	blit->copy(source, 0, destination, 0, 256);
	blit->endEncoding();
	blitBarriers = globalBarriers;

	globalBarriers = 0;
	auto compute = commandBuffer->computeCommandEncoder();
	compute->endEncoding();
	computeBarriers = globalBarriers;

	auto pass = renderPass(device);
	globalBarriers = 0;
	auto render = commandBuffer->renderCommandEncoder(*pass);
	render->endEncoding();
	renderBarriers = globalBarriers;

	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
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
		if (!Indium::DynamicVK::vkCmdPipelineBarrier2.resolve()) {
			return 3;
		}

		bool keepPollingDevice = true;
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		original = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(Indium::DynamicVK::vkCmdPipelineBarrier2.pointer);
		Indium::DynamicVK::vkCmdPipelineBarrier2.pointer = reinterpret_cast<void*>(recordBarrier);

		auto queue = device->newCommandQueue();

		unsigned blitBarriers, computeBarriers, renderBarriers;
		encodeEachKind(device, queue, blitBarriers, computeBarriers, renderBarriers);
		report(blitBarriers == 0, "a blit encoder does not end with a memory dependency on everything");
		report(computeBarriers == 0, "a compute encoder does not end with a memory dependency on everything");
		report(renderBarriers == 0, "a render encoder does not end with a memory dependency on everything");

		// a texture that is an image of its own, kept equal to a buffer's rows: 48 bytes is a row pitch that is not a
		// multiple of the unit the device's images use for it
		auto buffer = device->newBuffer(48 * 4, Indium::ResourceOptions::StorageModeShared);
		auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 5, 4, false);
		descriptor.resourceOptions = Indium::ResourceOptions::StorageModeShared;
		auto texture = buffer->newTexture(descriptor, 0, 48);
		if (!texture) {
			std::printf("SKIP the device cannot make a texture over a buffer at this pitch\n");
		} else {
			encodeEachKind(device, queue, blitBarriers, computeBarriers, renderBarriers);
			// the first encoder of a command buffer also orders what the command buffers before it did
			report(blitBarriers == 2, "with a texture kept equal to a buffer, a blit encoder is ordered after what came before and ends with a memory dependency");
			report(computeBarriers == 1, "with a texture kept equal to a buffer, a compute encoder ends with a memory dependency");
			report(renderBarriers == 1, "with a texture kept equal to a buffer, a render encoder ends with a memory dependency");
		}

		Indium::DynamicVK::vkCmdPipelineBarrier2.pointer = reinterpret_cast<void*>(original);

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
