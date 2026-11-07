// Copyright (C) 2026 Darling Developers
//
// Buffers and textures share device memory blocks:
//  - many small resources take few device memory allocations, a large one gets its own;
//  - resources sharing a block do not overlap, whether written by the CPU or by the GPU (a blit,
//    a render pass clear and a kernel reading and writing through buffer device addresses);
//  - freed ranges are reused without disturbing the resources still alive;
//  - didModifyRange on a Managed buffer flushes only whole multiples of nonCoherentAtomSize, as
//    vkFlushMappedMemoryRanges requires, covering at least the modified bytes.

#include "air-ops.h"

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>
#include <indium/device.private.hpp>
#include <indium/memory-pool.private.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

std::atomic<unsigned> memoryAllocations { 0 };
PFN_vkAllocateMemory originalAllocateMemory;
VKAPI_ATTR VkResult VKAPI_CALL countAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* allocator, VkDeviceMemory* memory) {
	++memoryAllocations;
	return originalAllocateMemory(device, info, allocator, memory);
}

std::mutex flushMutex;
std::vector<VkMappedMemoryRange> flushes;
PFN_vkFlushMappedMemoryRanges originalFlush;
VKAPI_ATTR VkResult VKAPI_CALL recordFlush(VkDevice device, uint32_t count, const VkMappedMemoryRange* ranges) {
	{
		std::scoped_lock lock(flushMutex);
		flushes.insert(flushes.end(), ranges, ranges + count);
	}
	return originalFlush(device, count, ranges);
}

void fillPattern(Indium::Buffer* buffer, uint8_t seed) {
	auto bytes = static_cast<uint8_t*>(buffer->contents());
	for (size_t i = 0; i < buffer->length(); ++i) {
		bytes[i] = static_cast<uint8_t>(seed + i * 7);
	}
}

bool hasPattern(Indium::Buffer* buffer, uint8_t seed) {
	auto bytes = static_cast<const uint8_t*>(buffer->contents());
	for (size_t i = 0; i < buffer->length(); ++i) {
		if (bytes[i] != static_cast<uint8_t>(seed + i * 7)) {
			return false;
		}
	}
	return true;
}

void testFlushRangeRule() {
	// a pooled allocation: granule-aligned offset and size
	Indium::MemoryPool::Allocation pooled;
	pooled.offset = 3 * Indium::MemoryPool::granule;
	pooled.size = 4 * Indium::MemoryPool::granule;
	bool ok = true;
	for (VkDeviceSize atom: { 1, 4, 64, 128, 256 }) {
		for (VkDeviceSize start = 0; start < pooled.size; start += 37) {
			for (VkDeviceSize length: { 0, 1, 5, 63, 64, 200, 1023 }) {
				if (start + length > pooled.size) {
					continue;
				}
				auto range = Indium::MemoryPool::flushRange(pooled, start, length, atom);
				if (length == 0) {
					ok = ok && range.size == 0;
					continue;
				}
				ok = ok && range.offset % atom == 0 && range.size % atom == 0;
				ok = ok && range.offset <= pooled.offset + start && range.offset + range.size >= pooled.offset + start + length;
				ok = ok && range.offset >= pooled.offset && range.offset + range.size <= pooled.offset + pooled.size;
			}
		}
	}
	report(ok, "flush ranges of a pooled allocation are whole atoms inside it, covering the modified bytes");

	// a dedicated allocation whose size is not a whole number of atoms: the last range ends with it
	Indium::MemoryPool::Allocation dedicated;
	dedicated.offset = 0;
	dedicated.size = 1000;
	auto range = Indium::MemoryPool::flushRange(dedicated, 990, 10, 64);
	report(range.offset == 960 && range.offset + range.size == 1000, "a flush at the end of a dedicated allocation ends with the memory");
}

void testBuffers(Indium::Device* device) {
	unsigned before = memoryAllocations;
	std::vector<std::shared_ptr<Indium::Buffer>> buffers;
	for (size_t i = 0; i < 200; ++i) {
		buffers.push_back(device->newBuffer(1000 + i, Indium::ResourceOptions::StorageModeShared));
		fillPattern(buffers.back().get(), static_cast<uint8_t>(i));
	}
	unsigned small = memoryAllocations - before;
	bool intact = true;
	for (size_t i = 0; i < buffers.size(); ++i) {
		intact = intact && hasPattern(buffers[i].get(), static_cast<uint8_t>(i));
	}
	report(small <= 2, "200 small buffers take at most 2 memory allocations");
	report(intact, "small buffers written by the CPU do not overlap");

	before = memoryAllocations;
	auto large = device->newBuffer(Indium::MemoryPool::maxPooledSize + 4096, Indium::ResourceOptions::StorageModeShared);
	report(memoryAllocations - before == 1, "a large buffer gets an allocation of its own");
	large.reset();

	// free every other buffer, allocate again: the ranges are reused, the others stay intact
	for (size_t i = 0; i < buffers.size(); i += 2) {
		buffers[i].reset();
	}
	before = memoryAllocations;
	for (size_t i = 0; i < buffers.size(); i += 2) {
		buffers[i] = device->newBuffer(1000 + i, Indium::ResourceOptions::StorageModeShared);
		fillPattern(buffers[i].get(), static_cast<uint8_t>(0x80 + i));
	}
	intact = true;
	for (size_t i = 0; i < buffers.size(); ++i) {
		intact = intact && hasPattern(buffers[i].get(), static_cast<uint8_t>((i % 2 ? 0 : 0x80) + i));
	}
	report(memoryAllocations - before == 0, "freed ranges are reused");
	report(intact, "reusing freed ranges leaves the other buffers intact");

	// the GPU sees each buffer at its own place: blit between neighbours
	auto queue = device->newCommandQueue();
	auto commandBuffer = queue->commandBuffer();
	auto blit = commandBuffer->blitCommandEncoder();
	for (size_t i = 0; i + 1 < 20; i += 2) {
		blit->copy(buffers[i + 1], 0, buffers[i], 0, 1000);
	}
	blit->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	bool copied = true;
	for (size_t i = 0; i + 1 < 20; i += 2) {
		auto bytes = static_cast<const uint8_t*>(buffers[i]->contents());
		auto source = static_cast<const uint8_t*>(buffers[i + 1]->contents());
		copied = copied && std::memcmp(bytes, source, 1000) == 0;
		// the bytes past the copy are untouched
		for (size_t j = 1000; j < buffers[i]->length(); ++j) {
			copied = copied && bytes[j] == static_cast<uint8_t>(0x80 + i + j * 7);
		}
		copied = copied && hasPattern(buffers[i + 1].get(), static_cast<uint8_t>(i + 1));
	}
	report(copied, "blits between buffers in one block touch only their own bytes");
}

// air-ops.metal's control_flow kernel through buffer device addresses of pooled buffers
void testKernel(Indium::Device* device) {
	constexpr size_t threadCount = 8;
	auto library = device->newLibrary(air_ops_metallib, air_ops_metallib_len);
	auto pipeline = device->newComputePipelineState(library->newFunction("control_flow"));
	auto queue = device->newCommandQueue();

	std::vector<std::shared_ptr<Indium::Buffer>> inputs, outputs;
	for (size_t i = 0; i < 16; ++i) {
		std::vector<int32_t> ns(threadCount);
		for (size_t t = 0; t < threadCount; ++t) {
			ns[t] = static_cast<int32_t>((i + t) % 9);
		}
		inputs.push_back(device->newBuffer(ns.data(), ns.size() * sizeof(int32_t), Indium::ResourceOptions::StorageModeShared));
		outputs.push_back(device->newBuffer(threadCount * 4 * sizeof(int32_t), Indium::ResourceOptions::StorageModeShared));
		std::memset(outputs.back()->contents(), 0xA5, outputs.back()->length());
	}

	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	for (size_t i = 0; i < inputs.size(); ++i) {
		encoder->setBuffer(outputs[i], 0, 0);
		encoder->setBuffer(inputs[i], 0, 1);
		encoder->dispatchThreads(Indium::Size { threadCount, 1, 1 }, Indium::Size { threadCount, 1, 1 });
	}
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();

	bool ok = true;
	for (size_t i = 0; i < inputs.size(); ++i) {
		auto out = static_cast<const int32_t*>(outputs[i]->contents());
		for (size_t id = 0; id < threadCount; ++id) {
			uint32_t n = (i + id) % 9;
			uint32_t squares = 0;
			for (uint32_t k = 0; k < n; ++k) {
				squares += k * k;
			}
			ok = ok && out[id * 4] == static_cast<int32_t>(squares) && out[id * 4 + 1] == static_cast<int32_t>((n & 1) ? n * 3 + 1 : n / 2);
		}
	}
	report(ok, "kernels read and write pooled buffers at their own addresses");
}

void testTextures(Indium::Device* device) {
	auto queue = device->newCommandQueue();
	unsigned before = memoryAllocations;
	std::vector<std::shared_ptr<Indium::Texture>> textures;
	for (size_t i = 0; i < 32; ++i) {
		auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 8, 8, false);
		descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
		textures.push_back(device->newTexture(descriptor));
	}
	report(memoryAllocations - before <= 4, "32 small textures take few memory allocations");

	for (size_t i = 0; i < textures.size(); ++i) {
		Indium::RenderPassDescriptor pass {};
		Indium::RenderPassColorAttachmentDescriptor attachment {};
		attachment.texture = textures[i];
		attachment.loadAction = Indium::LoadAction::Clear;
		attachment.storeAction = Indium::StoreAction::Store;
		attachment.clearColor = Indium::ClearColor(i / 255.0, 0, 1, 1);
		pass.colorAttachments.push_back(attachment);
		auto commandBuffer = queue->commandBuffer();
		commandBuffer->renderCommandEncoder(pass)->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
	}
	bool ok = true;
	for (size_t i = 0; i < textures.size(); ++i) {
		std::vector<uint32_t> pixels(64, 0);
		textures[i]->getBytes(Indium::Region::make2D(0, 0, 8, 8), 0, pixels.data(), 8 * 4);
		for (auto pixel: pixels) {
			ok = ok && pixel == (0xffff0000u | static_cast<uint32_t>(i));
		}
	}
	report(ok, "render passes into textures in one block touch only their own texture");
}

void testManagedFlush(Indium::Device* device) {
	auto atom = static_cast<Indium::PrivateDevice*>(device)->properties().limits.nonCoherentAtomSize;
	auto buffer = device->newBuffer(4000, Indium::ResourceOptions::StorageModeManaged);
	{
		std::scoped_lock lock(flushMutex);
		flushes.clear();
	}
	struct Modified { size_t start, length; };
	const Modified modified[] { { 3, 5 }, { 0, 1 }, { 1000, 1 }, { 255, 2 }, { 3990, 10 }, { 0, 4000 } };
	for (auto range: modified) {
		buffer->didModifyRange(Indium::Range<size_t> { range.start, range.length });
	}
	std::scoped_lock lock(flushMutex);
	bool ok = flushes.size() == sizeof(modified) / sizeof(modified[0]);
	for (size_t i = 0; ok && i < flushes.size(); ++i) {
		ok = flushes[i].offset % atom == 0 && (flushes[i].size % atom == 0 || flushes[i].size == VK_WHOLE_SIZE)
			&& flushes[i].size >= modified[i].length;
	}
	char what[128];
	std::snprintf(what, sizeof(what), "didModifyRange flushes whole nonCoherentAtomSize (%llu) multiples", static_cast<unsigned long long>(atom));
	report(ok, what);
}

}

int main() {
	Indium::init(nullptr, 0, false);

	testFlushRangeRule();

	{
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			return failures ? 1 : 0;
		}

		bool keepPollingDevice = true;
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		Indium::DynamicVK::vkAllocateMemory.resolve();
		originalAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(Indium::DynamicVK::vkAllocateMemory.pointer);
		Indium::DynamicVK::vkAllocateMemory.pointer = reinterpret_cast<void*>(countAllocateMemory);
		Indium::DynamicVK::vkFlushMappedMemoryRanges.resolve();
		originalFlush = reinterpret_cast<PFN_vkFlushMappedMemoryRanges>(Indium::DynamicVK::vkFlushMappedMemoryRanges.pointer);
		Indium::DynamicVK::vkFlushMappedMemoryRanges.pointer = reinterpret_cast<void*>(recordFlush);

		testBuffers(device.get());
		testKernel(device.get());
		testTextures(device.get());
		testManagedFlush(device.get());

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	std::printf("%s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
