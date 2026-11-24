// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in bool-conversions.metallib, which sign- and zero-extend booleans (AIR's i1) and
// convert them to floating point, and compares each result with the conversion as LLVM defines it,
// computed here on the host.
// Prints SKIP and exits 0 without a Vulkan device.

#include "bool-conversions.h"

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
		auto library = device->newLibrary(bool_conversions_metallib, bool_conversions_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			std::vector<int32_t> values { 0, 1, 2, 3, -1, -2, INT32_MIN, 0x40000001 };

			std::vector<int32_t> extend, toFloat;
			for (auto v: values) {
				uint32_t u = static_cast<uint32_t>(v);
				bool negative = v < 0;
				bool negative2 = static_cast<int32_t>(u << 1) < 0;
				// sext to i32, the high word of sext to i64, sext to i16 and i8 zero-extended, a vector sext, zext
				extend.insert(extend.end(), { negative ? -1 : 0, negative ? -1 : 0, negative ? 0xFFFF : 0, negative ? 0xFF : 0, negative ? -1 : 0, negative2 ? -1 : 0, negative ? 1 : 0 });
				// the bits of uitofp and sitofp to float and to half, and of both on a vector
				toFloat.insert(toFloat.end(), { negative ? 0x3F800000 : 0, negative ? static_cast<int32_t>(0xBF800000u) : 0, negative ? 0x3C00 : 0, negative ? 0xBC00 : 0, negative2 ? 0x3F800000 : 0, negative2 ? static_cast<int32_t>(0xBF800000u) : 0 });
			}
			check(device.get(), queue.get(), library.get(), "extend_bool", values, extend, "sext and zext of i1 and <2 x i1>");
			check(device.get(), queue.get(), library.get(), "bool_to_float", values, toFloat, "uitofp and sitofp of i1 and <2 x i1> to float and half");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
