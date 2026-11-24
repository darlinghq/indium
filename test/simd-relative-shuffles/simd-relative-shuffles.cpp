// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in simd-relative-shuffles.metallib over one threadgroup of 64 threads: simd_shuffle_down and
// simd_shuffle_up, which leave the lanes with no lane to read from unchanged, and simd_shuffle_and_fill_down and
// simd_shuffle_and_fill_up, which shuffle within vectors of `modulo` lanes and fill the lanes past the end of a
// vector from a second value. The host compares every thread's results with what the Metal Shading Language
// specification defines for the simdgroup width of the device.
// Prints SKIP and exits 0 without a Vulkan device.

#include "simd-relative-shuffles.h"

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
		auto library = device->newLibrary(simd_relative_shuffles_metallib, simd_relative_shuffles_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// the values thread t shuffles: data and filling
			auto data = [](size_t t) { return static_cast<uint32_t>(t * 7 + 1000); };
			auto filling = [](size_t t) { return static_cast<uint32_t>(t * 7 + 65536); };

			// shuffle_down by 1 and 3, shuffle_up by 1 and 3
			auto upDown = [&](size_t t, size_t width) {
				size_t lane = t % width;
				return std::vector<uint32_t> {
					lane + 1 < width ? data(t + 1) : data(t),
					lane + 3 < width ? data(t + 3) : data(t),
					lane >= 1 ? data(t - 1) : data(t),
					lane >= 3 ? data(t - 3) : data(t),
				};
			};
			check(device.get(), queue.get(), library.get(), "shuffle_up_down", 5, "shuffle_down and shuffle_up of integers keep the lanes at the ends", upDown);
			check(device.get(), queue.get(), library.get(), "shuffle_up_down_float", 5, "shuffle_down and shuffle_up of floats keep the lanes at the ends", upDown);

			// shuffle_and_fill_down with (delta, modulo) (1, 2), (1, 4), (3, 8), then shuffle_and_fill_up with (1, 2), (3, 4),
			// (2, 8), then shuffle_and_fill_down with (3, width) and shuffle_and_fill_up with (2, width)
			auto fill = [&](size_t t, size_t width) {
				auto down = [&](size_t delta, size_t modulo) {
					size_t position = t % modulo, first = t - position;
					return position + delta < modulo ? data(first + position + delta) : filling(first + position + delta - modulo);
				};
				auto up = [&](size_t delta, size_t modulo) {
					size_t position = t % modulo, first = t - position;
					return position >= delta ? data(first + position - delta) : filling(first + position + modulo - delta);
				};
				return std::vector<uint32_t> { down(1, 2), down(1, 4), down(3, 8), up(1, 2), up(3, 4), up(2, 8), down(3, width), up(2, width) };
			};
			check(device.get(), queue.get(), library.get(), "shuffle_and_fill", 9, "shuffle_and_fill_down and shuffle_and_fill_up of integers", fill);
			check(device.get(), queue.get(), library.get(), "shuffle_and_fill_float", 9, "shuffle_and_fill_down and shuffle_and_fill_up of floats", fill);
			// the same values truncated to 16 bits
			check(device.get(), queue.get(), library.get(), "shuffle_and_fill_short", 9, "shuffle_and_fill_down and shuffle_and_fill_up of 16-bit integers", [&](size_t t, size_t width) {
				auto want = fill(t, width);
				for (auto& word: want) {
					word &= 0xFFFF;
				}
				return want;
			});
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
