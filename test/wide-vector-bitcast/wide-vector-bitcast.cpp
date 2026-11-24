// Copyright (C) 2026 Darling Developers
//
// Runs the kernel in wide-vector-bitcast.metallib, which extracts elements from bitcasts to vectors of
// more than four elements, and compares each element with the bits it is made of, computed here on the
// host.
// Prints SKIP and exits 0 without a Vulkan device.

#include "wide-vector-bitcast.h"

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
		auto library = device->newLibrary(wide_vector_bitcast_metallib, wide_vector_bitcast_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			std::vector<int32_t> values { 0, 1, -1, 0x12345678, static_cast<int32_t>(0x89ABCDEFu), 0x00FF00FF, INT32_MIN, 0x7F7F7F7F };

			std::vector<int32_t> want;
			for (size_t i = 0; i < values.size(); ++i) {
				uint32_t v = static_cast<uint32_t>(values[i]);
				uint32_t w[3] = { v, v * 3, ~v };
				auto byte = [&](size_t n) { return static_cast<int32_t>((w[n / 4] >> (8 * (n % 4))) & 0xFF); };
				auto half = [&](size_t n) { return static_cast<uint16_t>(w[n / 2] >> (16 * (n % 2))); };
				uint64_t q = static_cast<uint64_t>(v) << 32 | (v * 3);
				// bytes 0, 5, 11 and i * 5 % 12 of w; half-word 3 and (sign-extended) i % 6; byte 6 of q; byte 4 of (v, ~v); half-word 1 of w; byte 1 of w if v is negative, else byte 0
				want.insert(want.end(), { byte(0), byte(5), byte(11), byte(i * 5 % 12), half(3), static_cast<int16_t>(half(i % 6)), static_cast<int32_t>((q >> 48) & 0xFF), static_cast<int32_t>(~v & 0xFF), half(1), byte(values[i] < 0 ? 1 : 0) });
			}
			check(device.get(), queue.get(), library.get(), "wide_vector_bitcast", values, want, "elements of bitcasts to <12 x i8>, <6 x i16>, <8 x i8> and <6 x half>");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
