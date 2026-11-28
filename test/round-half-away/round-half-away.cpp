// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in round-half-away.metallib, which call air.round (MSL's round) on float, half and a float
// vector, and compares the results with round as the Metal Shading Language specification defines it: to the
// nearest integer, with halfway cases rounded away from zero.
// Prints SKIP and exits 0 without a Vulkan device.

#include "round-half-away.h"

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

// The result a slot should hold: `value` rounded to the kernel's float type, bit for bit when `exact` (NaN
// matching any NaN, and a subnormal also a zero of its sign, since Metal may flush subnormals), otherwise within the kernel's tolerance, relative to the
// result or to 1, whichever is larger (Vulkan bounds the error of some functions absolutely, e.g. sin's).
struct Expected {
	double value;
	bool exact;
	// whether +0 is right too: the input was subnormal, and may have been flushed
	bool orZero = false;
};

// Runs `kernel` with one thread per input value, reading `want.size() / input.size()` float (`half` false) or
// half (`half` true) results per thread, and compares them with `want`.
void checkFloat(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, const std::vector<int32_t>& input, const std::vector<Expected>& want, bool half, double tolerance, const std::string& what) {
	try {
		size_t slots = want.size() / input.size();
		auto got = run(device, queue, library, kernel, input, slots);
		bool ok = true;
		for (size_t i = 0; i < want.size(); ++i) {
			double value;
			uint32_t bits, wantBits;
			if (half) {
				uint16_t raw = static_cast<uint16_t>(got[i]);
				_Float16 h, w = static_cast<_Float16>(want[i].value);
				std::memcpy(&h, &raw, 2);
				uint16_t rawWant;
				std::memcpy(&rawWant, &w, 2);
				value = h;
				bits = static_cast<uint32_t>(got[i]);
				wantBits = rawWant;
			} else {
				float f, w = static_cast<float>(want[i].value);
				std::memcpy(&f, &got[i], 4);
				std::memcpy(&wantBits, &w, 4);
				value = f;
				bits = static_cast<uint32_t>(got[i]);
			}
			bool match;
			if (std::isnan(want[i].value)) {
				match = std::isnan(value);
			} else if (want[i].exact) {
				uint32_t signBit = half ? 0x8000u : 0x80000000u, exponentBits = half ? 0x7c00u : 0x7f800000u;
				bool subnormal = (wantBits & exponentBits) == 0;
				match = bits == wantBits || (subnormal && bits == (wantBits & signBit)) || (want[i].orZero && bits == 0);
			} else {
				match = std::fabs(value - want[i].value) <= tolerance * std::max(std::fabs(want[i].value), 1.0);
			}
			if (!match) {
				std::printf("  %s: input 0x%08x slot %zu: got %a (0x%x), expected %a%s\n", kernel, static_cast<uint32_t>(input[i / slots]), i % slots, value, bits, want[i].value, want[i].exact ? " exactly" : "");
				ok = false;
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
		auto library = device->newLibrary(round_half_away_metallib, round_half_away_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// halfway cases of both signs, with even and odd integer parts, their neighbours, zeros, integers too large
			// to have a fraction, infinities and a NaN
			std::vector<double> values { 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49999997, -0.49999997, 0.50000006, 2.4999998, 8388607.5, -8388606.5, 0.0, -0.0, 0.3, -0.3, 1e10, -16777215.0, 7.5, -100.5, INFINITY, -INFINITY, NAN, 3.7 };
			std::vector<double> valuesHalf { 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49975586, -0.49975586, 0.50048828, 2.4980469, 1023.5, -1022.5, 0.0, -0.0, 0.3, -0.3, 60000.0, -2047.0, 7.5, -100.5, INFINITY, -INFINITY, NAN, 3.7 };

			for (bool half: { false, true }) {
				std::vector<int32_t> bits;
				std::vector<Expected> want;
				for (double v: half ? valuesHalf : values) {
					double x;
					if (half) {
						_Float16 h = static_cast<_Float16>(v);
						uint16_t b;
						std::memcpy(&b, &h, 2);
						bits.push_back(b);
						x = h;
					} else {
						float f = static_cast<float>(v);
						int32_t b;
						std::memcpy(&b, &f, 4);
						bits.push_back(b);
						x = f;
					}
					// std::round rounds halfway cases away from zero
					if (half) {
						want.push_back({ std::round(x), true });
					} else {
						want.insert(want.end(), { { std::round(x), true }, { std::round(x), true }, { std::round(x), true }, { std::round(-x), true } });
					}
				}
				checkFloat(device.get(), queue.get(), library.get(), half ? "round_f16" : "round_f32", bits, want, half, 0, half ? "round of half" : "round and fast round of float and <2 x float>");
			}
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
