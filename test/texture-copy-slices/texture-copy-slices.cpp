// Copyright (C) 2026 Darling Developers
//
// A blit copy from one texture to another writes the destination's slice and level, which need not be the
// source's (Metal's copyFromTexture:sourceSlice:sourceLevel:...toTexture:destinationSlice:destinationLevel:...,
// which WebKit's WebGL uses to fill the faces of a cube map and the slices of an array texture). This copies
// slice 0, level 0 of a 2D texture into slice 2, level 1 of an array texture and checks that:
//  - the barriers that move the destination image into and out of the transfer destination layout name the
//    slice and level the copy writes (Vulkan requires the image to be in that layout where the copy writes;
//    a driver may otherwise write it wrongly, and the validation layer reports it);
//  - the copied texels are in that slice and level, and every other slice and level keeps its contents.

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

PFN_vkCmdPipelineBarrier original;
std::vector<VkImageMemoryBarrier> destinationBarriers;

// the image barriers into and out of the transfer destination layout
void VKAPI_CALL recordBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
	VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
	uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers,
	uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
	for (uint32_t i = 0; i < imageMemoryBarrierCount; ++i) {
		const auto& barrier = pImageMemoryBarriers[i];
		if (barrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || barrier.oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			destinationBarriers.push_back(barrier);
		}
	}
	original(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers,
		bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t size = 8;
constexpr size_t slices = 3;
constexpr size_t levels = 2;

uint32_t fill(size_t slice, size_t level) {
	return 0xff000000u | static_cast<uint32_t>(slice * 0x10 + level + 1);
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

		auto queue = device->newCommandQueue();

		auto sourceDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, size / 2, size / 2, false);
		auto source = device->newTexture(sourceDescriptor);
		std::vector<uint32_t> copied(size / 2 * size / 2, 0xff336699u);
		source->replaceRegion(Indium::Region::make2D(0, 0, size / 2, size / 2), 0, copied.data(), size / 2 * 4);

		auto destinationDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, size, size, false);
		destinationDescriptor.textureType = Indium::TextureType::e2DArray;
		destinationDescriptor.arrayLength = slices;
		destinationDescriptor.mipmapLevelCount = levels;
		auto destination = device->newTexture(destinationDescriptor);
		for (size_t slice = 0; slice < slices; ++slice) {
			for (size_t level = 0; level < levels; ++level) {
				size_t side = size >> level;
				std::vector<uint32_t> texels(side * side, fill(slice, level));
				destination->replaceRegion(Indium::Region::make2D(0, 0, side, side), level, slice, texels.data(), side * 4, side * side * 4);
			}
		}

		original = reinterpret_cast<PFN_vkCmdPipelineBarrier>(Indium::DynamicVK::vkCmdPipelineBarrier.pointer);
		Indium::DynamicVK::vkCmdPipelineBarrier.pointer = reinterpret_cast<void*>(recordBarrier);

		auto commandBuffer = queue->commandBuffer();
		auto blit = commandBuffer->blitCommandEncoder();
		blit->copy(source, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { size / 2, size / 2, 1 }, destination, 2, 1, Indium::Origin { 0, 0, 0 });
		blit->synchronize(destination);
		blit->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();

		Indium::DynamicVK::vkCmdPipelineBarrier.pointer = reinterpret_cast<void*>(original);

		bool sawBarrier = !destinationBarriers.empty();
		bool rangesMatch = sawBarrier;
		for (const auto& barrier: destinationBarriers) {
			const auto& range = barrier.subresourceRange;
			if (range.baseArrayLayer != 2 || range.layerCount != 1 || range.baseMipLevel != 1 || range.levelCount != 1) {
				rangesMatch = false;
			}
		}
		report(rangesMatch, "the barriers of the copy's destination name the slice and level it writes");

		bool otherSlicesKept = true;
		bool copiedThere = true;
		for (size_t slice = 0; slice < slices; ++slice) {
			for (size_t level = 0; level < levels; ++level) {
				size_t side = size >> level;
				std::vector<uint32_t> got(side * side);
				destination->getBytes(Indium::Region::make2D(0, 0, side, side), level, slice, got.data(), side * 4, side * side * 4);
				for (size_t y = 0; y < side; ++y) {
					for (size_t x = 0; x < side; ++x) {
						uint32_t expected = slice == 2 && level == 1 && x < size / 2 && y < size / 2 ? 0xff336699u : fill(slice, level);
						if (got[y * side + x] != expected) {
							(slice == 2 && level == 1 ? copiedThere : otherSlicesKept) = false;
						}
					}
				}
			}
		}
		report(copiedThere, "the copied texels are in the destination slice and level");
		report(otherSlicesKept, "the other slices and levels keep their contents");

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
