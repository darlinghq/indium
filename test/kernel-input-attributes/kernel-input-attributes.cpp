// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in kernel-input-attributes.metallib, whose parameters are the quad-group built-ins and the
// dispatch_ variants of the threadgroup's sizes, over threadgroups of 64 and of 7x3 threads, and compares every
// thread's values with what the Metal Shading Language specification defines: quad-groups are four
// consecutive threads of a simdgroup.
// Prints SKIP and exits 0 without a Vulkan device.

#include "kernel-input-attributes.h"

#include <indium/indium.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	failures += !ok;
}

// Runs `kernel` over one threadgroup of the given size and returns the `slots` words each thread wrote.
std::vector<uint32_t> run(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, size_t slots, Indium::Size group) {
	size_t threads = group.width * group.height * group.depth;
	auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
	if (!pipeline) {
		throw std::runtime_error(std::string(kernel) + " did not load");
	}
	std::vector<uint32_t> poison(threads * slots, 0xA5A5A5A5u);
	auto out = device->newBuffer(poison.data(), poison.size() * sizeof(uint32_t), Indium::ResourceOptions::StorageModeShared);
	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(out, 0, 0);
	encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, group);
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	std::vector<uint32_t> result(poison.size());
	std::memcpy(result.data(), out->contents(), result.size() * sizeof(uint32_t));
	return result;
}

// Runs `kernel` over a threadgroup of the given size; its last slot is the simdgroup width. Compares the other slots
// of every thread t (its index in the threadgroup) with `expect(t, width)`.
void check(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, size_t slots, Indium::Size group, const std::string& what, const std::function<std::vector<uint32_t>(size_t t, size_t width)>& expect) {
	try {
		size_t threads = group.width * group.height * group.depth;
		auto got = run(device, queue, library, kernel, slots, group);
		size_t width = got[slots - 1];
		bool ok = width >= 4 && width <= 128 && (width & (width - 1)) == 0;
		if (!ok) {
			std::printf("  %s: unusable simdgroup width %zu\n", kernel, width);
		}
		for (size_t t = 0; ok && t < threads; ++t) {
			auto want = expect(t, width);
			want.push_back(width);
			for (size_t k = 0; k < slots; ++k) {
				if (got[t * slots + k] != want[k]) {
					std::printf("  %s (width %zu): thread %zu slot %zu: got 0x%08x, expected 0x%08x\n", kernel, width, t, k, got[t * slots + k], want[k]);
					ok = false;
				}
			}
		}
		report(ok, std::string(kernel) + " (" + std::to_string(group.width) + "x" + std::to_string(group.height) + "x" + std::to_string(group.depth) + "): " + what);
	} catch (const std::exception& e) {
		report(false, std::string(kernel) + ": " + e.what());
	}
}

}

int main() {
	Indium::init(nullptr, 0, false);

	{
		if (Indium::copyAllDevices().empty()) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}

		auto device = Indium::createSystemDefaultDevice();
		std::printf("device: %s\n", device->name().c_str());
		// the bounded wait lets the thread see the flag even if the wakeup comes before it waits
		std::atomic<bool> keepPollingDevice { true };
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(100000000);
			}
		});
		auto queue = device->newCommandQueue();
		auto library = device->newLibrary(kernel_input_attributes_metallib, kernel_input_attributes_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			for (auto group: { Indium::Size { 64, 1, 1 }, Indium::Size { 7, 3, 1 } }) {
				size_t threads = group.width * group.height * group.depth;
				// the lane in the quad-group, the quad-group, the quad-groups in the threadgroup (twice), the simdgroups in it
				auto quads = [&](size_t t, size_t width) {
					uint32_t quadgroups = (threads + 3) / 4, simdgroups = (threads + width - 1) / width;
					return std::vector<uint32_t> { static_cast<uint32_t>(t % 4), static_cast<uint32_t>(t / 4), quadgroups, quadgroups, simdgroups };
				};
				check(device.get(), queue.get(), library.get(), "quad_ids", 6, group, "quad-group built-ins as 32-bit integers", quads);
				check(device.get(), queue.get(), library.get(), "quad_ids_short", 6, group, "quad-group built-ins as 16-bit integers", quads);
				// the threadgroup size given at dispatch
				check(device.get(), queue.get(), library.get(), "grid_sizes", 4, group, "dispatch_threads_per_threadgroup as 32-bit integers", [&](size_t, size_t) {
					return std::vector<uint32_t> { static_cast<uint32_t>(group.width), static_cast<uint32_t>(group.height), static_cast<uint32_t>(group.depth) };
				});
				check(device.get(), queue.get(), library.get(), "grid_sizes_short", 2, group, "dispatch_threads_per_threadgroup as a 16-bit integer", [&](size_t, size_t) {
					return std::vector<uint32_t> { static_cast<uint32_t>(group.width) };
				});
			}
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
