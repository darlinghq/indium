// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in overflow-intrinsics.metallib, which call llvm.<s|u><add|sub|mul>.with.overflow on
// 16, 32 and 64-bit integers, and compares each wrapped result and overflow flag with the intrinsic as
// LLVM defines it, computed here on the host.
// Prints SKIP and exits 0 without a Vulkan device.

#include "overflow-intrinsics.h"

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
		auto library = device->newLibrary(overflow_intrinsics_metallib, overflow_intrinsics_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// thread i pairs values[i] with values[(i + 8) % 16], chosen so that every overflow flag is set for some pairs and clear for others
			std::vector<int32_t> values { 46341, 2, 0x10000, 0, 32767, INT32_MAX, 2, static_cast<int32_t>(0xFFFF8000u), -1, 5, INT32_MIN, -46341, 46341, 32768, 0, INT32_MAX };

			// the wrapped result and the overflow flag of each operation, signed and unsigned add, sub and mul
			auto expected = [](auto a, auto b, auto store) {
				using S = decltype(a);
				using U = std::make_unsigned_t<S>;
				S s;
				U u;
				bool o;
				o = __builtin_add_overflow(a, b, &s); store(static_cast<U>(s), o);
				o = __builtin_add_overflow(static_cast<U>(a), static_cast<U>(b), &u); store(u, o);
				o = __builtin_sub_overflow(a, b, &s); store(static_cast<U>(s), o);
				o = __builtin_sub_overflow(static_cast<U>(a), static_cast<U>(b), &u); store(u, o);
				o = __builtin_mul_overflow(a, b, &s); store(static_cast<U>(s), o);
				o = __builtin_mul_overflow(static_cast<U>(a), static_cast<U>(b), &u); store(u, o);
			};

			std::vector<int32_t> want16, want32, want64;
			for (size_t i = 0; i < values.size(); ++i) {
				int32_t a = values[i], b = values[(i + 8) % values.size()];
				expected(static_cast<int16_t>(a), static_cast<int16_t>(b), [&](uint16_t v, bool o) { want16.insert(want16.end(), { v, o }); });
				expected(a, b, [&](uint32_t v, bool o) { want32.insert(want32.end(), { static_cast<int32_t>(v), o }); });
				// a vector multiplication with lanes (a, b) and (a, 2): the second lane's result and both flags
				int32_t a2;
				bool o0 = __builtin_mul_overflow(a, b, &a2);
				bool o1 = __builtin_mul_overflow(a, 2, &a2);
				want32.insert(want32.end(), { a2, o0, o1 });
				// a = a * 2^32 + b, b = b * 2^32 + a
				int64_t a64 = static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(a)) << 32 | static_cast<uint32_t>(b));
				int64_t b64 = static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(b)) << 32 | static_cast<uint32_t>(a));
				expected(a64, b64, [&](uint64_t v, bool o) { want64.insert(want64.end(), { static_cast<int32_t>(v), static_cast<int32_t>(v >> 32), o }); });
			}
			check(device.get(), queue.get(), library.get(), "overflow_i16", values, want16, "add, sub and mul with overflow of 16-bit integers");
			check(device.get(), queue.get(), library.get(), "overflow_i32", values, want32, "add, sub and mul with overflow of 32-bit integers and vectors");
			check(device.get(), queue.get(), library.get(), "overflow_i64", values, want64, "add, sub and mul with overflow of 64-bit integers");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
