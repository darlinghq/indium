// Copyright (C) 2026 Lubos Dolezel
//
// Runs the kernel in air-unpack.metallib, which unpacks known 32-bit values with the AIR forms of the MSL unpack
// functions, and compares every channel with the conversion formulas of the Metal Shading Language specification.

#include "air-unpack.h"

#include <indium/indium.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

static constexpr size_t slots = 16;

static float unorm(uint32_t v, uint32_t bits) {
	return v / static_cast<float>((1u << bits) - 1);
}

static float snorm(int32_t v, uint32_t bits) {
	return std::max(v / static_cast<float>((1 << (bits - 1)) - 1), -1.0f);
}

static float srgbToLinear(float c) {
	return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static std::vector<float> expected(const std::vector<uint32_t>& values) {
	std::vector<float> want;
	for (auto v: values) {
		for (uint32_t i = 0; i < 4; ++i) {
			want.push_back(unorm((v >> (8 * i)) & 0xff, 8));
		}
		for (uint32_t i = 0; i < 4; ++i) {
			want.push_back(snorm(static_cast<int8_t>((v >> (8 * i)) & 0xff), 8));
		}
		for (uint32_t i = 0; i < 2; ++i) {
			want.push_back(unorm((v >> (16 * i)) & 0xffff, 16));
		}
		for (uint32_t i = 0; i < 2; ++i) {
			want.push_back(snorm(static_cast<int16_t>((v >> (16 * i)) & 0xffff), 16));
		}
		for (uint32_t i = 0; i < 4; ++i) {
			float c = unorm((v >> (8 * i)) & 0xff, 8);
			want.push_back(i < 3 ? srgbToLinear(c) : c);
		}
	}
	return want;
}

int main() {
	Indium::init(nullptr, 0, ENABLE_VALIDATION);

	// zero, all ones, the extremes of each signed field (-128 and -32768 clamp to -1), and mixed values
	std::vector<uint32_t> values { 0x00000000, 0xffffffff, 0x80808080, 0x7f7f7f7f, 0x80007fff, 0x01020304, 0xff00ff00, 0xdeadbeef };
	auto want = expected(values);
	bool ok = true;

	{
		auto device = Indium::createSystemDefaultDevice();
		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		try {
			std::vector<float> poison(want.size(), -42.0f);
			auto library = device->newLibrary(air_unpack_metallib, air_unpack_metallib_len);
			auto pipeline = device->newComputePipelineState(library->newFunction("unpack"));
			auto queue = device->newCommandQueue();
			auto output = device->newBuffer(poison.data(), poison.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);
			auto input = device->newBuffer(values.data(), values.size() * sizeof(uint32_t), Indium::ResourceOptions::StorageModeShared);

			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->computeCommandEncoder();
			encoder->setComputePipelineState(pipeline);
			encoder->setBuffer(output, 0, 0);
			encoder->setBuffer(input, 0, 1);
			encoder->dispatchThreads(Indium::Size { values.size(), 1, 1 }, Indium::Size { values.size(), 1, 1 });
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();

			std::vector<float> got(want.size());
			std::memcpy(got.data(), output->contents(), got.size() * sizeof(float));
			size_t bad = 0;
			for (size_t i = 0; i < want.size(); ++i) {
				if (!(std::fabs(got[i] - want[i]) <= 1e-5f * std::fmax(1.0f, std::fabs(want[i])))) {
					if (bad++ < 16) {
						std::cerr << "value 0x" << std::hex << values[i / slots] << std::dec << " slot " << i % slots << ": got " << got[i] << ", expected " << want[i] << std::endl;
					}
				}
			}
			std::cout << "unpack: " << (want.size() - bad) << " of " << want.size() << " results correct" << std::endl;
			ok = bad == 0;
		} catch (const std::exception& e) {
			std::cerr << "unpack: " << e.what() << std::endl;
			ok = false;
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	std::cout << (ok ? "All results as expected" : "Some results were wrong") << std::endl;
	return ok ? 0 : 1;
};
