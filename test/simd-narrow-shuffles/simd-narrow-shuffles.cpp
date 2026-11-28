// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in simd-narrow-shuffles.metallib, which broadcast and shuffle 8 and 16-bit integers and halfs
// across a simdgroup, over one threadgroup of 64 threads, and compares every thread's results with what the
// Metal Shading Language specification defines for the simdgroup width of the device.
// Prints SKIP and exits 0 without a Vulkan device.

#include "simd-narrow-shuffles.h"

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
		auto library = device->newLibrary(simd_narrow_shuffles_metallib, simd_narrow_shuffles_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// the value thread t shuffles, as the kernel stores it: zero-extended integers, halfs as the bits of a float
			auto value8 = [](size_t t) { return static_cast<uint32_t>((t * 37 + 133) & 0xFF); };
			auto value16 = [](size_t t) { return static_cast<uint32_t>((t * 1031 + 0x8003) & 0xFFFF); };
			auto valueHalf = [](size_t t) {
				float f = t + 0.5f;
				uint32_t bits;
				std::memcpy(&bits, &f, sizeof(bits));
				return bits;
			};

			// shuffle from lane + 1 (wrapping), shuffle_xor 1, shuffle_down 1, shuffle_up 1, broadcast from lane 3 and from the
			// last lane, and broadcast_first
			auto shuffles = [](const std::function<uint32_t(size_t)>& value) {
				return [value](size_t t, size_t width) {
					size_t first = t - t % width, lane = t % width;
					return std::vector<uint32_t> {
						value(first + (lane + 1) % width),
						value(t ^ 1),
						// shuffles that read outside the simdgroup are not compared
						lane + 1 < width ? value(t + 1) : UNDEFINED,
						lane > 0 ? value(t - 1) : UNDEFINED,
						value(first + 3),
						value(first + width - 1),
						value(first),
					};
				};
			};

			check(device.get(), queue.get(), library.get(), "shuffle_s_i8", 8, "broadcasts and shuffles of signed 8-bit integers", shuffles(value8));
			check(device.get(), queue.get(), library.get(), "shuffle_u_i8", 8, "broadcasts and shuffles of unsigned 8-bit integers", shuffles(value8));
			check(device.get(), queue.get(), library.get(), "shuffle_s_i16", 8, "broadcasts and shuffles of signed 16-bit integers", shuffles(value16));
			check(device.get(), queue.get(), library.get(), "shuffle_u_i16", 8, "broadcasts and shuffles of unsigned 16-bit integers", shuffles(value16));
			check(device.get(), queue.get(), library.get(), "shuffle_f16", 8, "broadcasts and shuffles of halfs", shuffles(valueHalf));
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
