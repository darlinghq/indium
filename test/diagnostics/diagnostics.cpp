// Copyright (C) 2026 Lubos Dolezel
//
// Diagnostics:
//  - Device::currentAllocatedSize() counts the bytes of live buffers, textures and heaps, and goes down again when
//    they are released: a resource adds at least its allocatedSize(), a texture view and a resource made from a
//    heap add nothing, a heap adds its whole size;
//  - Device::statistics() counts allocations, memory objects and heaps, and keeps the peak;
//  - pipeline timing is off by default, records nothing while off, and when turned on adds the GPU time of each
//    pipeline's dispatches (and runs of draws), by pipeline name, once the command buffer has completed.

#include "compute-lib.h"

#include <indium/indium.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

using namespace Indium;

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr auto privateOptions = ResourceOptions::StorageModePrivate | ResourceOptions::CPUCacheModeDefaultCache;

void testAllocatedSize(Device& device) {
	const uint64_t base = device.currentAllocatedSize();
	auto stats0 = device.statistics();
	report(stats0.allocatedBytes == base, "statistics().allocatedBytes equals currentAllocatedSize()");

	{
		auto small = device.newBuffer(1 << 20, privateOptions);
		report(device.currentAllocatedSize() - base == (1u << 20), "a 1 MiB buffer adds 1 MiB");
		auto large = device.newBuffer(8 << 20, privateOptions);
		report(device.currentAllocatedSize() - base == (9u << 20), "an 8 MiB buffer adds 8 MiB (an allocation of its own)");
		report(small->allocatedSize() == (1u << 20) && large->allocatedSize() == (8u << 20), "the buffers report those sizes");

		auto stats = device.statistics();
		report(stats.liveAllocations == stats0.liveAllocations + 2 && stats.totalAllocations == stats0.totalAllocations + 2, "two allocations counted");
		report(stats.peakAllocatedBytes >= base + (9u << 20), "the peak covers the live total");

		auto before = device.currentAllocatedSize();
		large.reset();
		report(before - device.currentAllocatedSize() == (8u << 20), "releasing the 8 MiB buffer takes 8 MiB off");
		small.reset();
	}
	report(device.currentAllocatedSize() == base, "everything released: back to the starting value");
	report(device.statistics().peakAllocatedBytes >= base + (9u << 20), "the peak stays after the release");

	{
		auto descriptor = TextureDescriptor::texture2DDescriptor(PixelFormat::RGBA8Unorm, 256, 256, false);
		descriptor.resourceOptions = privateOptions;
		descriptor.usage = TextureUsage::ShaderRead;
		auto texture = device.newTexture(descriptor);
		auto added = device.currentAllocatedSize() - base;
		report(texture && added >= 256 * 256 * 4 && added >= texture->allocatedSize(), "a texture adds at least its size");

		auto view = texture->newTextureView(PixelFormat::RGBA8Unorm_sRGB);
		report(!view || device.currentAllocatedSize() - base == added, "a texture view adds nothing");
	}
	report(device.currentAllocatedSize() == base, "texture and view released: back to the starting value");
}

void testHeap(Device& device) {
	const uint64_t base = device.currentAllocatedSize();
	const auto stats0 = device.statistics();

	HeapDescriptor descriptor;
	descriptor.size = 4 << 20;
	descriptor.storageMode = StorageMode::Private;
	descriptor.type = HeapType::Automatic;
	{
		auto heap = device.newHeap(descriptor);
		if (!heap) {
			std::printf("SKIP no heap\n");
			return;
		}
		report(device.currentAllocatedSize() - base == heap->currentAllocatedSize(), "a heap adds its whole size");
		auto stats = device.statistics();
		report(stats.liveHeaps == stats0.liveHeaps + 1 && stats.liveHeapBytes == stats0.liveHeapBytes + heap->size() && stats.totalHeaps == stats0.totalHeaps + 1, "the heap is counted");

		auto buffer = heap->newBuffer(1 << 20, privateOptions);
		report(buffer && device.currentAllocatedSize() - base == heap->currentAllocatedSize(), "a buffer made from the heap adds nothing");
		buffer.reset();
		report(device.currentAllocatedSize() - base == heap->currentAllocatedSize(), "releasing it changes nothing");
	}
	report(device.currentAllocatedSize() == base, "heap released: back to the starting value");
	auto stats = device.statistics();
	report(stats.liveHeaps == stats0.liveHeaps && stats.liveHeapBytes == stats0.liveHeapBytes && stats.totalHeaps == stats0.totalHeaps + 1, "the heap is no longer live, still counted as made");
}

void runAdd(Device& device, CommandQueue& queue, const std::shared_ptr<ComputePipelineState>& state, const std::shared_ptr<Buffer>& buffer, unsigned times) {
	auto commandBuffer = queue.commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(state);
	encoder->setBuffer(buffer, 0, 0);
	encoder->setBuffer(buffer, 0, 1);
	encoder->setBuffer(buffer, 0, 2);
	for (unsigned i = 0; i < times; ++i) {
		encoder->dispatchThreads(Size { 64, 1, 1 }, Size { 64, 1, 1 });
	}
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
}

void testPipelineTiming(Device& device, CommandQueue& queue) {
	report(!device.pipelineTimingEnabled(), "pipeline timing is off by default");

	auto library = device.newLibrary(compute_lib, compute_lib_len);
	auto state = device.newComputePipelineState(library->newFunction("add_arrays"));
	auto buffer = device.newBuffer(64 * sizeof(float), ResourceOptions::StorageModeShared);

	runAdd(device, queue, state, buffer, 2);
	// a command buffer retires after its handlers have run, which is after waitUntilCompleted() returns
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	report(device.pipelineTimings().empty(), "nothing is recorded while timing is off");

	if (!device.supportsPipelineTiming()) {
		std::printf("SKIP the device cannot time pipelines\n");
		device.setPipelineTimingEnabled(true);
		report(!device.pipelineTimingEnabled(), "enabling does nothing on a device that cannot time");
		return;
	}

	device.setPipelineTimingEnabled(true);
	report(device.pipelineTimingEnabled(), "timing can be turned on");
	runAdd(device, queue, state, buffer, 3);
	// timings are added when the command buffer retires
	std::vector<PipelineTiming> timings;
	for (int i = 0; i < 100 && timings.empty(); ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		timings = device.pipelineTimings();
	}
	report(timings.size() == 1 && timings[0].name == "compute add_arrays", "one entry, named after the kernel");
	report(!timings.empty() && timings[0].spans == 3, "one span per dispatch");

	device.setPipelineTimingEnabled(false);
	runAdd(device, queue, state, buffer, 2);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	timings = device.pipelineTimings();
	report(timings.size() == 1 && timings[0].spans == 3, "turned off again: nothing more is recorded");
}

}

int main() {
	Indium::init(nullptr, 0, false);

	{
		auto device = createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			return 0;
		}

		std::atomic<bool> polling { true };
		std::thread poller([&] {
			while (polling) {
				device->pollEvents(UINT64_MAX);
			}
		});

		auto queue = device->newCommandQueue();
		report(device->statistics().allocatedBytes == 0 || device->statistics().allocatedBytes == device->currentAllocatedSize(), "statistics are readable at the start");

		testAllocatedSize(*device);
		testHeap(*device);
		testPipelineTiming(*device, *queue);

		polling = false;
		device->wakeupEventLoop();
		poller.join();
	}

	Indium::finit();

	std::printf("%s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
