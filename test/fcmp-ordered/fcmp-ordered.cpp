// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in fcmp-ordered.metallib, which compare float, half and float vector values with fcmp ord
// and fcmp uno, and compares the results with the comparisons as LLVM defines them: ord is true if neither
// operand is a NaN, uno if either is.
// Prints SKIP and exits 0 without a Vulkan device.

#include "fcmp-ordered.h"

#include <indium/indium.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
		auto library = device->newLibrary(fcmp_ordered_metallib, fcmp_ordered_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// the bits of float and half NaNs, infinities, zeros and ordinary values; thread i compares values[i] with
			// values[(i + 8) % 16], which pairs NaNs with numbers, numbers with NaNs, and NaNs with NaNs
			const uint32_t nan32 = 0x7fc00000, negativeNan32 = 0xffc00001, inf32 = 0x7f800000;
			const uint16_t nan16 = 0x7e00, negativeNan16 = 0xfe01, inf16 = 0x7c00;
			std::vector<uint32_t> floats { nan32, 0x3f800000, 0, 0x80000000, inf32, negativeNan32, 0x40490fdb, 0xbf000000, nan32, negativeNan32, 0xc2c80000, 0x00000001, nan32, 0xff800000, 0x3e800000, 0x7f7fffff };
			std::vector<uint32_t> halves { nan16, 0x3c00, 0, 0x8000, inf16, negativeNan16, 0x4248, 0xb800, nan16, negativeNan16, 0xd640, 0x0001, nan16, 0xfc00, 0x3400, 0x7bff };

			auto isNan32 = [](uint32_t bits) { return (bits & 0x7fffffffu) > 0x7f800000u; };
			auto isNan16 = [](uint32_t bits) { return (bits & 0x7fffu) > 0x7c00u; };

			for (bool half: { false, true }) {
				auto& bits = half ? halves : floats;
				auto isNan = half ? +isNan16 : +isNan32;
				std::vector<int32_t> input(bits.begin(), bits.end());
				std::vector<int32_t> want;
				for (size_t i = 0; i < bits.size(); ++i) {
					bool a = isNan(bits[i]), b = isNan(bits[(i + 8) % bits.size()]);
					// ord a b, uno a b, ord a a, uno a a, ord a 0, uno 1 b
					want.insert(want.end(), { !a && !b, a || b, !a, a, !a, b });
				}
				check(device.get(), queue.get(), library.get(), half ? "ordered_f16" : "ordered_f32", input, want, std::string("fcmp ord and uno of ") + (half ? "half" : "float"));
			}

			{
				std::vector<int32_t> input(floats.begin(), floats.end());
				std::vector<int32_t> want;
				for (size_t i = 0; i < floats.size(); ++i) {
					bool a = isNan32(floats[i]), b = isNan32(floats[(i + 8) % floats.size()]);
					// lanes (a, b) and (b, NaN)
					want.insert(want.end(), { !a && !b, false, a || b, true });
				}
				check(device.get(), queue.get(), library.get(), "ordered_v2f32", input, want, "fcmp ord and uno of <2 x float>");
			}
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
