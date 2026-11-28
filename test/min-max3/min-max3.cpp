// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in min-max3.metallib, which call air.min3, air.max3 and air.median3 (MSL's min3, max3 and
// median3), signed and unsigned, on 8, 16, 32 and 64-bit integers and on a vector of 32-bit integers, and
// compares the results with the functions computed here on the host.
// Prints SKIP and exits 0 without a Vulkan device.

#include "min-max3.h"

#include <indium/indium.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cmath>
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
		auto library = device->newLibrary(min_max3_metallib, min_max3_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// thread i takes values[i], values[(i + 8) % 16] and values[(i + 4) % 16], whose order differs between threads,
			// and whose narrow truncations and signed and unsigned orders differ too; threads 0, 4, 8 and 12 have ties
			std::vector<int32_t> values { 0x80, 1, -1, 0x7f, 0x80, 0x7fff, 0x8000, INT32_MAX, INT32_MIN, 0x12345678, -0x12345678, 0xff, INT32_MIN, 5, -5, static_cast<int32_t>(0x80000080u) };

			// min3, max3 and median3 of a, b and c, signed then unsigned for each
			auto expected = [](auto a, auto b, auto c, auto store) {
				using S = decltype(a);
				using U = std::make_unsigned_t<S>;
				auto median = [](auto x, auto y, auto z) {
					decltype(x) sorted[] = { x, y, z };
					std::sort(sorted, sorted + 3);
					return sorted[1];
				};
				U ua = a, ub = b, uc = c;
				store(std::min({ a, b, c }), std::min({ ua, ub, uc }));
				store(std::max({ a, b, c }), std::max({ ua, ub, uc }));
				store(median(a, b, c), median(ua, ub, uc));
			};

			std::vector<int32_t> want8, want16, want32, want64, wantVector;
			for (size_t i = 0; i < values.size(); ++i) {
				int32_t a = values[i], b = values[(i + 8) % 16], c = values[(i + 4) % 16];
				// narrow results come back sign-extended if signed and zero-extended if not
				expected(static_cast<int8_t>(a), static_cast<int8_t>(b), static_cast<int8_t>(c), [&](int8_t s, uint8_t u) { want8.insert(want8.end(), { s, u }); });
				expected(static_cast<int16_t>(a), static_cast<int16_t>(b), static_cast<int16_t>(c), [&](int16_t s, uint16_t u) { want16.insert(want16.end(), { s, u }); });
				expected(a, b, c, [&](int32_t s, uint32_t u) { want32.insert(want32.end(), { s, static_cast<int32_t>(u) }); });
				// a = a:b, b = b:c, c = c:a as high:low halves
				auto join = [](int32_t hi, int32_t lo) { return static_cast<int64_t>(static_cast<uint64_t>(static_cast<uint32_t>(hi)) << 32 | static_cast<uint32_t>(lo)); };
				expected(join(a, b), join(b, c), join(c, a), [&](int64_t s, uint64_t u) {
					want64.insert(want64.end(), { static_cast<int32_t>(s), static_cast<int32_t>(s >> 32), static_cast<int32_t>(u), static_cast<int32_t>(u >> 32) });
				});
				// lanes (a, b, c) and (b, c, a)
				std::vector<int32_t> lane0, lane1;
				expected(a, b, c, [&](int32_t s, uint32_t u) { lane0.insert(lane0.end(), { s, static_cast<int32_t>(u) }); });
				expected(b, c, a, [&](int32_t s, uint32_t u) { lane1.insert(lane1.end(), { s, static_cast<int32_t>(u) }); });
				for (size_t k = 0; k < lane0.size(); ++k) {
					wantVector.insert(wantVector.end(), { lane0[k], lane1[k] });
				}
			}
			check(device.get(), queue.get(), library.get(), "minmax3_i8", values, want8, "min3, max3 and median3 of 8-bit integers");
			check(device.get(), queue.get(), library.get(), "minmax3_i16", values, want16, "min3, max3 and median3 of 16-bit integers");
			check(device.get(), queue.get(), library.get(), "minmax3_i32", values, want32, "min3, max3 and median3 of 32-bit integers");
			check(device.get(), queue.get(), library.get(), "minmax3_i64", values, want64, "min3, max3 and median3 of 64-bit integers");
			check(device.get(), queue.get(), library.get(), "minmax3_v2i32", values, wantVector, "min3, max3 and median3 of <2 x i32>");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
