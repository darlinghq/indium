// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in bit-counts.metallib, which call popcount, clz, ctz and reverse_bits on 8, 16 and
// 64-bit integers, and compares each result with the definition in the Metal Shading Language
// specification, computed here on the host.
// Prints SKIP and exits 0 without a Vulkan device.

#include "bit-counts.h"

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
		auto library = device->newLibrary(bit_counts_metallib, bit_counts_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			std::vector<int32_t> values { 0, 1, 0x80, 0x8000, INT32_MIN, -1, 0x12345678, 0x00F00F00 };

			auto clz = [](uint64_t v, unsigned bits) { unsigned n = 0; for (int i = bits - 1; i >= 0 && !((v >> i) & 1); --i) ++n; return static_cast<int32_t>(n); };
			auto ctz = [](uint64_t v, unsigned bits) { unsigned n = 0; for (unsigned i = 0; i < bits && !((v >> i) & 1); ++i) ++n; return static_cast<int32_t>(n); };
			auto popcount = [](uint64_t v) { return static_cast<int32_t>(__builtin_popcountll(v)); };
			auto reverse = [](uint64_t v, unsigned bits) { uint64_t r = 0; for (unsigned i = 0; i < bits; ++i) r |= ((v >> i) & 1) << (bits - 1 - i); return r; };

			std::vector<int32_t> want8, want16, want64;
			for (auto v: values) {
				uint32_t u = static_cast<uint32_t>(v);
				uint64_t x8 = u & 0xFF, x16 = u & 0xFFFF, h16 = u >> 16;
				want8.insert(want8.end(), { popcount(x8), clz(x8, 8), ctz(x8, 8), static_cast<int32_t>(reverse(x8, 8)) });
				want16.insert(want16.end(), { popcount(x16), clz(x16, 16), ctz(x16, 16), static_cast<int32_t>(reverse(x16, 16)), clz(x16, 16), clz(h16, 16), ctz(h16, 16) });
				uint64_t a = u, b = a << 32, c = a | b;
				uint64_t d = b | (u ^ 0x0F0F0F0Fu);
				uint64_t ra = reverse(d, 64);
				want64.insert(want64.end(), { popcount(a), popcount(b), popcount(c), clz(a, 64), clz(b, 64), clz(c, 64), ctz(a, 64), ctz(b, 64), ctz(c, 64), static_cast<int32_t>(ra), static_cast<int32_t>(ra >> 32) });
			}
			check(device.get(), queue.get(), library.get(), "counts_i8", values, want8, "popcount, clz, ctz, reverse_bits of 8-bit integers");
			check(device.get(), queue.get(), library.get(), "counts_i16", values, want16, "popcount, clz, ctz, reverse_bits of 16-bit integers and vectors");
			check(device.get(), queue.get(), library.get(), "counts_i64", values, want64, "popcount, clz, ctz, reverse_bits of 64-bit integers");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
