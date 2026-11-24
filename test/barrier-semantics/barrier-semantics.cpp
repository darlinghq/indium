// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in barrier-semantics.metallib, which store between threadgroup and simdgroup barriers and
// fences that order no memory or only memory Vulkan doesn't have, and fences of every scope, over one
// threadgroup of 64 threads, and checks that every store happened.
// Prints SKIP and exits 0 without a Vulkan device.

#include "barrier-semantics.h"

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

constexpr size_t threads = 64;

// Runs `kernel` over one threadgroup of `threads` threads and returns the `slots` words each thread wrote.
std::vector<uint32_t> run(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, size_t slots) {
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
	encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { threads, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	std::vector<uint32_t> result(poison.size());
	std::memcpy(result.data(), out->contents(), result.size() * sizeof(uint32_t));
	return result;
}

// Runs `kernel`, whose last slot is the simdgroup width, and compares the other slots of every thread t with
// `expect(t, width)`; UNDEFINED marks a slot whose value the specification leaves open.
constexpr uint32_t UNDEFINED = 0xFFFFFFFFu;
void check(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, size_t slots, const std::string& what, const std::function<std::vector<uint32_t>(size_t t, size_t width)>& expect) {
	try {
		auto got = run(device, queue, library, kernel, slots);
		size_t width = got[slots - 1];
		bool ok = width >= 4 && width <= threads && (width & (width - 1)) == 0;
		if (!ok) {
			std::printf("  %s: unusable simdgroup width %zu\n", kernel, width);
		}
		for (size_t t = 0; ok && t < threads; ++t) {
			auto want = expect(t, width);
			want.push_back(width);
			for (size_t k = 0; k < slots; ++k) {
				if (want[k] != UNDEFINED && got[t * slots + k] != want[k]) {
					std::printf("  %s (width %zu): thread %zu slot %zu: got 0x%08x, expected 0x%08x\n", kernel, width, t, k, got[t * slots + k], want[k]);
					ok = false;
				}
			}
		}
		report(ok, std::string(kernel) + ": " + what);
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
		auto library = device->newLibrary(barrier_semantics_metallib, barrier_semantics_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// each kernel stores 1, 2, ... around its barriers or fences
			auto stores = [](size_t count) {
				return [count](size_t, size_t) {
					std::vector<uint32_t> want;
					for (uint32_t k = 1; k <= count; ++k) {
						want.push_back(k);
					}
					return want;
				};
			};
			check(device.get(), queue.get(), library.get(), "barriers", 8, "threadgroup and simdgroup barriers of no memory, of imageblock memory, of threadgroup and device memory", stores(7));
			check(device.get(), queue.get(), library.get(), "fences", 7, "fences of no memory, of imageblock memory, relaxed, of device and of threadgroup memory", stores(6));
			check(device.get(), queue.get(), library.get(), "fence_scopes", 6, "fences of the thread and of the simdgroup", stores(5));
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
