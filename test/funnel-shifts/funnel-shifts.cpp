// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in funnel-shifts.metallib, which call llvm.fshl and llvm.fshr on 8, 16, 32 and 64-bit
// integers and on a vector of 32-bit integers, and compares the results with the funnel shifts as LLVM defines
// them, computed here on the host.
// Prints SKIP and exits 0 without a Vulkan device.

#include "funnel-shifts.h"

#include <indium/indium.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	failures += !ok;
}

// Runs `kernel` with one thread per input value and returns `slots` results per thread.
std::vector<int32_t> run(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, const std::vector<int32_t>& input, size_t slots) {
	auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
	if (!pipeline) {
		throw std::runtime_error(std::string(kernel) + " did not load");
	}
	std::vector<int32_t> poison(input.size() * slots);
	std::memset(poison.data(), 0xA5, poison.size() * sizeof(int32_t));
	auto in = device->newBuffer(input.data(), input.size() * sizeof(int32_t), Indium::ResourceOptions::StorageModeShared);
	auto out = device->newBuffer(poison.data(), poison.size() * sizeof(int32_t), Indium::ResourceOptions::StorageModeShared);
	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(out, 0, 0);
	encoder->setBuffer(in, 0, 1);
	encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { input.size(), 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	std::vector<int32_t> result(poison.size());
	std::memcpy(result.data(), out->contents(), result.size() * sizeof(int32_t));
	return result;
}

// Runs `kernel` and compares its results with `want`, `slots` per input value.
void check(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, const std::vector<int32_t>& input, const std::vector<int32_t>& want, const std::string& what) {
	try {
		size_t slots = want.size() / input.size();
		auto got = run(device, queue, library, kernel, input, slots);
		for (size_t i = 0; i < want.size(); ++i) {
			if (got[i] != want[i]) {
				std::printf("  %s: input 0x%08x slot %zu: got 0x%08x, expected 0x%08x\n", kernel, static_cast<uint32_t>(input[i / slots]), i % slots, static_cast<uint32_t>(got[i]), static_cast<uint32_t>(want[i]));
			}
		}
		report(got == want, std::string(kernel) + ": " + what);
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
		auto library = device->newLibrary(funnel_shifts_metallib, funnel_shifts_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// thread i shifts values[i] and values[(i + 8) % 16] by values[(i + 4) % 16], which is 0, a multiple of 8 or 32,
			// or larger than the width for some threads
			std::vector<int32_t> values { 0, 1, -1, 0x7f, 0x80, 0x7fff, 0x8000, INT32_MAX, INT32_MIN, 0x12345678, -0x12345678, 0xff, 0x100, 5, -5, static_cast<int32_t>(0x80000080u) };

			// fshl and fshr of a (the upper half), b (the lower half) and n, at the width of T
			auto expected = [](auto a, auto b, auto n, auto store) {
				using U = std::make_unsigned_t<decltype(a)>;
				constexpr unsigned width = sizeof(U) * 8;
				U ua = a, ub = b;
				unsigned shift = static_cast<U>(n) % width;
				store(shift ? static_cast<U>(ua << shift | ub >> (width - shift)) : ua);
				store(shift ? static_cast<U>(ua << (width - shift) | ub >> shift) : ub);
			};

			std::vector<int32_t> want8, want16, want32, want64, wantVector;
			for (size_t i = 0; i < values.size(); ++i) {
				int32_t a = values[i], b = values[(i + 8) % 16], c = values[(i + 4) % 16];
				expected(static_cast<int8_t>(a), static_cast<int8_t>(b), static_cast<int8_t>(c), [&](uint8_t v) { want8.push_back(v); });
				expected(static_cast<int16_t>(a), static_cast<int16_t>(b), static_cast<int16_t>(c), [&](uint16_t v) { want16.push_back(v); });
				expected(a, b, c, [&](uint32_t v) { want32.push_back(static_cast<int32_t>(v)); });
				// a = a:b, b = b:c, c = c:a as high:low halves
				auto join = [](int32_t hi, int32_t lo) { return static_cast<int64_t>(static_cast<uint64_t>(static_cast<uint32_t>(hi)) << 32 | static_cast<uint32_t>(lo)); };
				expected(join(a, b), join(b, c), join(c, a), [&](uint64_t v) { want64.insert(want64.end(), { static_cast<int32_t>(v), static_cast<int32_t>(v >> 32) }); });
				// lanes (a, b, c) and (b, c, a)
				std::vector<int32_t> lane0, lane1;
				expected(a, b, c, [&](uint32_t v) { lane0.push_back(static_cast<int32_t>(v)); });
				expected(b, c, a, [&](uint32_t v) { lane1.push_back(static_cast<int32_t>(v)); });
				for (size_t k = 0; k < lane0.size(); ++k) {
					wantVector.insert(wantVector.end(), { lane0[k], lane1[k] });
				}
			}
			check(device.get(), queue.get(), library.get(), "funnel_i8", values, want8, "fshl and fshr of 8-bit integers");
			check(device.get(), queue.get(), library.get(), "funnel_i16", values, want16, "fshl and fshr of 16-bit integers");
			check(device.get(), queue.get(), library.get(), "funnel_i32", values, want32, "fshl and fshr of 32-bit integers");
			check(device.get(), queue.get(), library.get(), "funnel_i64", values, want64, "fshl and fshr of 64-bit integers");
			check(device.get(), queue.get(), library.get(), "funnel_v2i32", values, wantVector, "fshl and fshr of <2 x i32>");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
