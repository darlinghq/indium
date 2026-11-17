// Copyright (C) 2026 Lubos Dolezel
//
// Kernels from bit-ops.metallib: popcount, clz, ctz, reverse_bits, extract_bits, insert_bits, mulhi and rotate
// on unsigned and signed integers, and arithmetic on half values. Each result is compared with the
// definition in the Metal Shading Language specification, computed here on the host.
// Prints SKIP and exits 0 without a Vulkan device.

#include "bit-ops.h"

#include <indium/indium.hpp>

#include <cmath>
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

template<typename T>
std::vector<T> run(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, const std::vector<T>& input, size_t outputCount) {
	auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
	if (!pipeline) {
		throw std::runtime_error(std::string(kernel) + " did not load");
	}
	std::vector<T> poison(outputCount);
	std::memset(poison.data(), 0xA5, poison.size() * sizeof(T));
	auto in = device->newBuffer(input.data(), input.size() * sizeof(T), Indium::ResourceOptions::StorageModeShared);
	auto out = device->newBuffer(poison.data(), poison.size() * sizeof(T), Indium::ResourceOptions::StorageModeShared);
	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(in, 0, 0);
	encoder->setBuffer(out, 0, 1);
	encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { input.size(), 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	std::vector<T> result(outputCount);
	std::memcpy(result.data(), out->contents(), result.size() * sizeof(T));
	return result;
}

uint32_t reverseBits(uint32_t v) {
	uint32_t r = 0;
	for (int i = 0; i < 32; ++i) {
		r |= ((v >> i) & 1u) << (31 - i);
	}
	return r;
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
		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});
		auto queue = device->newCommandQueue();
		auto library = device->newLibrary(bit_ops_metallib, bit_ops_metallib_len);
		report(library != nullptr, "the library loads");

		try {
			std::vector<uint32_t> values { 0, 1, 0x80000000u, 0xFFFFFFFFu, 0x12345678u, 0xF0F0F0F0u, 0x00010000u, 40 };
			std::vector<uint32_t> want;
			for (auto v: values) {
				want.push_back(__builtin_popcount(v));
				want.push_back(v ? __builtin_clz(v) : 32);
				want.push_back(v ? __builtin_ctz(v) : 32);
				want.push_back(reverseBits(v));
				want.push_back((v >> 4) & 0xFF);
				want.push_back((v & ~(0xFu << 8)) | (0xAu << 8));
				want.push_back(static_cast<uint32_t>((static_cast<uint64_t>(v) * 0x9E3779B9u) >> 32));
				want.push_back((v << 5) | (v >> 27));
			}
			auto got = run(device.get(), queue.get(), library.get(), "bit_ops", values, want.size());
			bool ok = got == want;
			for (size_t i = 0; !ok && i < want.size(); ++i) {
				if (got[i] != want[i]) {
					std::printf("  value %zu slot %zu: got %u, expected %u\n", i / 8, i % 8, got[i], want[i]);
				}
			}
			report(ok, "bit_ops: popcount, clz, ctz, reverse_bits, extract_bits, insert_bits, mulhi, rotate (unsigned)");
		} catch (const std::exception& e) {
			report(false, std::string("bit_ops: ") + e.what());
		}

		try {
			std::vector<int32_t> values { 0, 1, -1, INT32_MIN, 0x12345678, static_cast<int32_t>(0xF0F0F0F0u), 0x00000F80, -77 };
			std::vector<int32_t> want;
			for (auto v: values) {
				uint32_t u = static_cast<uint32_t>(v);
				want.push_back(__builtin_popcount(u));
				// the field is sign-extended for a signed value
				int32_t field = static_cast<int32_t>((u >> 4) & 0xFF);
				want.push_back(field >= 128 ? field - 256 : field);
				want.push_back(u ? __builtin_clz(u) : 32);
				want.push_back(static_cast<int32_t>((static_cast<int64_t>(v) * -3) >> 32));
			}
			auto got = run(device.get(), queue.get(), library.get(), "signed_bit_ops", values, want.size());
			report(got == want, "signed_bit_ops: popcount, extract_bits, clz, mulhi on signed values");
		} catch (const std::exception& e) {
			report(false, std::string("signed_bit_ops: ") + e.what());
		}

		try {
			std::vector<float> values { 0, 1, 2, 3, 4, 5, -1, -2.5f };
			std::vector<float> want;
			for (auto a: values) {
				float b = 2.5f;
				want.push_back(a * b + 0.5f);
				want.push_back(std::fmax(a, b));
				want.push_back(a * a + b * b + (a + b) * (a + b) + (a * b) * (a * b));
				want.push_back(std::floor(a) + std::fabs(b));
			}
			auto got = run(device.get(), queue.get(), library.get(), "half_ops", values, want.size());
			bool ok = true;
			for (size_t i = 0; i < want.size(); ++i) {
				ok = ok && std::fabs(got[i] - want[i]) <= 2e-3f * std::fmax(1.0f, std::fabs(want[i]));
			}
			report(ok, "half_ops: arithmetic, max, dot, floor and abs on half values");
		} catch (const std::exception& e) {
			report(false, std::string("half_ops: ") + e.what());
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
