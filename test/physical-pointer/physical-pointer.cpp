// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in physical-pointer.metallib on the system default device. Each kernel reaches device memory
// through a pointer whose pointee the module doesn't declare (see shadersrc/physical-pointer.ll) and writes one int
// per thread; the host computes the expected value of each.

#include "physical-pointer.h"

#include <indium/indium.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

static constexpr size_t threadCount = 32;

template<typename T>
static std::shared_ptr<Indium::Buffer> buffer(Indium::Device* device, const std::vector<T>& contents) {
	return device->newBuffer(contents.data(), contents.size() * sizeof(T), Indium::ResourceOptions::StorageModeShared);
}

// Runs `name` on one threadgroup of threadCount threads with the output in buffer 0 and `inputs` in buffers 1 and up.
// `indirect` are the buffers the kernel reaches only through addresses held in memory. Compares out[i] with want(i).
static bool check(Indium::Device* device, std::shared_ptr<Indium::Library> library, const char* name,
	const std::vector<std::shared_ptr<Indium::Buffer>>& inputs, const std::vector<std::shared_ptr<Indium::Buffer>>& indirect,
	const std::function<int32_t(int32_t)>& want)
{
	auto pipeline = device->newComputePipelineState(library->newFunction(name));
	auto queue = device->newCommandQueue();
	auto output = buffer(device, std::vector<int32_t>(threadCount, -999));

	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(output, 0, 0);
	for (size_t i = 0; i < inputs.size(); ++i) {
		encoder->setBuffer(inputs[i], 0, i + 1);
	}
	if (!indirect.empty()) {
		encoder->useResources(std::vector<std::shared_ptr<Indium::Resource>>(indirect.begin(), indirect.end()), Indium::ResourceUsage::Read);
	}
	encoder->dispatchThreads(Indium::Size { threadCount, 1, 1 }, Indium::Size { threadCount, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();

	auto got = static_cast<const int32_t*>(output->contents());
	size_t bad = 0;
	for (size_t i = 0; i < threadCount; ++i) {
		if (got[i] != want(i) && bad++ < 8) {
			std::cerr << name << ": thread " << i << ": got " << got[i] << ", expected " << want(i) << std::endl;
		}
	}
	std::cout << (bad ? "FAIL " : "PASS ") << name << std::endl;
	return bad == 0;
}

// the argument structs of shadersrc/physical-pointer.ll
struct Params { int32_t scale; float bias; };
struct Array { uint64_t data; int32_t scale; };
struct Args { uint64_t data; };

int main(int argc, char** argv) {
	Indium::init(nullptr, 0, ENABLE_VALIDATION);

	bool ok = true;

	{
		auto device = Indium::createSystemDefaultDevice();

		std::atomic<bool> keepPollingDevice { true };
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		auto library = device->newLibrary(physical_pointer_metallib, physical_pointer_metallib_len);

		// 32 ints followed by 32 floats
		std::vector<int32_t> ints(threadCount);
		std::vector<float> floats(threadCount);
		for (size_t i = 0; i < threadCount; ++i) {
			ints[i] = 3 * static_cast<int32_t>(i) + 1;
			floats[i] = 10.0f * i + 0.5f;
		}
		std::vector<uint8_t> mixed(threadCount * 8);
		std::memcpy(mixed.data(), ints.data(), threadCount * 4);
		std::memcpy(mixed.data() + threadCount * 4, floats.data(), threadCount * 4);
		auto data = buffer(device.get(), mixed);
		auto intBuffer = buffer(device.get(), ints);

		auto params = buffer(device.get(), std::vector<Params> { { 3, 7.9f } });
		ok = check(device.get(), library, "untyped_argument", { params }, {}, [](int32_t i) { return 3 * i + 7; }) && ok;

		auto array = buffer(device.get(), std::vector<Array> { { data->gpuAddress(), 2 } });
		ok = check(device.get(), library, "loaded_pointer", { array }, { data },
			[&](int32_t i) { return (ints[i] + static_cast<int32_t>(floats[i])) * 2; }) && ok;

		auto args = buffer(device.get(), std::vector<Args> { { intBuffer->gpuAddress() } });
		ok = check(device.get(), library, "constant_argument", { args }, { intBuffer }, [&](int32_t i) { return ints[i]; }) && ok;

		auto slots = buffer(device.get(), std::vector<uint64_t>(threadCount));
		ok = check(device.get(), library, "stored_pointer", { intBuffer, slots }, {}, [&](int32_t i) { return ints[i]; }) && ok;

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	std::cout << (ok ? "All results as expected" : "Some results were wrong") << std::endl;

	return ok ? 0 : 1;
}
