// Copyright (C) 2026 Darling Developers
//
// Distributed under the terms in the LICENSE file at the root of this repository.

#include "air-reinterpreted-local.h"

#include <indium/indium.hpp>

#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

static constexpr size_t count = 8;

struct Counters {
	uint32_t count;
	float scale;
};

static bool runLoad(Indium::Device* device, Indium::Library* lib) {
	std::vector<float> in(count * 4);
	for (size_t i = 0; i < in.size(); ++i)
		in[i] = 1.0f + static_cast<float>(i) * 0.5f;
	std::vector<float> poison(count * 4, -1.0f);
	Counters counters { 0, 2.0f };

	auto outBuffer = device->newBuffer(poison.data(), poison.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);
	auto inBuffer = device->newBuffer(in.data(), in.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);
	auto countersBuffer = device->newBuffer(&counters, sizeof(counters), Indium::ResourceOptions::StorageModeShared);

	auto pso = device->newComputePipelineState(lib->newFunction("reinterpret_load"));
	auto commandQueue = device->newCommandQueue();
	auto cmdbuf = commandQueue->commandBuffer();
	auto encoder = cmdbuf->computeCommandEncoder();
	encoder->setComputePipelineState(pso);
	encoder->setBuffer(outBuffer, 0, 0);
	encoder->setBuffer(inBuffer, 0, 1);
	encoder->setBuffer(countersBuffer, 0, 2);
	encoder->dispatchThreads(Indium::Size { count, 1, 1 }, Indium::Size { count, 1, 1 });
	encoder->endEncoding();
	cmdbuf->commit();
	cmdbuf->waitUntilCompleted();

	std::vector<float> out(count * 4);
	std::memcpy(out.data(), outBuffer->contents(), out.size() * sizeof(float));
	bool ok = true;
	for (size_t i = 0; i < count; ++i) {
		float want[4] = { in[i * 4], in[i * 4 + 1], in[i * 4 + 2], in[i * 4 + 3] * counters.scale };
		if (std::memcmp(&out[i * 4], want, sizeof(want)) != 0) {
			std::cerr << "reinterpret_load ERROR: element " << i << " is " << out[i * 4] << ", " << out[i * 4 + 1] << ", "
				<< out[i * 4 + 2] << ", " << out[i * 4 + 3] << std::endl;
			ok = false;
		}
	}
	Counters after {};
	std::memcpy(&after, countersBuffer->contents(), sizeof(after));
	if (after.count != 7) {
		std::cerr << "reinterpret_load ERROR: the store to the class-typed buffer wrote " << after.count << ", not 7" << std::endl;
		ok = false;
	}
	return ok;
}

static bool runStore(Indium::Device* device, Indium::Library* lib) {
	std::vector<float> in(count * 4);
	for (size_t i = 0; i < in.size(); ++i)
		in[i] = 3.0f + static_cast<float>(i) * 0.25f;
	std::vector<float> poison(count * 4, -1.0f);

	auto outBuffer = device->newBuffer(poison.data(), poison.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);
	auto inBuffer = device->newBuffer(in.data(), in.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);

	auto pso = device->newComputePipelineState(lib->newFunction("reinterpret_store"));
	auto commandQueue = device->newCommandQueue();
	auto cmdbuf = commandQueue->commandBuffer();
	auto encoder = cmdbuf->computeCommandEncoder();
	encoder->setComputePipelineState(pso);
	encoder->setBuffer(outBuffer, 0, 0);
	encoder->setBuffer(inBuffer, 0, 1);
	encoder->dispatchThreads(Indium::Size { count, 1, 1 }, Indium::Size { count, 1, 1 });
	encoder->endEncoding();
	cmdbuf->commit();
	cmdbuf->waitUntilCompleted();

	std::vector<float> out(count * 4);
	std::memcpy(out.data(), outBuffer->contents(), out.size() * sizeof(float));
	bool ok = true;
	for (size_t i = 0; i < out.size(); ++i) {
		if (out[i] != in[i]) {
			std::cerr << "reinterpret_store ERROR: float " << i << " is " << out[i] << ", expected " << in[i] << std::endl;
			ok = false;
		}
	}
	return ok;
}

int main(int argc, char** argv) {
	Indium::init(nullptr, 0, ENABLE_VALIDATION);

	bool ok = true;

	{
		auto device = Indium::createSystemDefaultDevice();

		bool keepPollingDevice = true;

		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		try {
			auto lib = device->newLibrary(air_reinterpreted_local, air_reinterpreted_local_len);
			bool load = runLoad(device.get(), lib.get());
			std::cout << "load through a re-typed local pointer and a class-typed buffer: " << (load ? "ok" : "FAILED") << std::endl;
			bool store = runStore(device.get(), lib.get());
			std::cout << "store through a re-typed local pointer: " << (store ? "ok" : "FAILED") << std::endl;
			ok = load && store;
		} catch (const std::exception& e) {
			std::cerr << "ERROR: " << e.what() << std::endl;
			ok = false;
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	std::cout << "Execution finished" << std::endl;

	return ok ? 0 : 1;
}
