#include "add.h"

#include <indium/indium.hpp>

#include <thread>
#include <functional>
#include <iostream>

// DO NOT COMMIT
#define ENABLE_VALIDATION 1

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION 0
#endif

static constexpr unsigned int arrayLength = 1 << 24;
static constexpr unsigned int bufferSize = arrayLength * sizeof(float);

int main(int argc, char** argv) {
	Indium::init(nullptr, 0, ENABLE_VALIDATION);

	{
		auto device = Indium::createSystemDefaultDevice();

		bool keepPollingDevice = true;

		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		auto lib = device->newLibrary(compute_add, compute_add_len);
		auto func = lib->newFunction("add_arrays");

		auto pso = device->newComputePipelineState(func);
		auto commandQueue = device->newCommandQueue();

		auto bufA = device->newBuffer(bufferSize, Indium::ResourceOptions::StorageModeShared);
		auto bufB = device->newBuffer(bufferSize, Indium::ResourceOptions::StorageModeShared);
		auto bufResult = device->newBuffer(bufferSize, Indium::ResourceOptions::StorageModeShared);

		auto generateRandomFloatData = [](std::shared_ptr<Indium::Buffer> buffer) {
			auto data = static_cast<float*>(buffer->contents());

			for (size_t i = 0; i < arrayLength; ++i) {
				data[i] = (float)rand() / (float)RAND_MAX;
			}
		};

		generateRandomFloatData(bufA);
		generateRandomFloatData(bufB);

		auto cmdbuf = commandQueue->commandBuffer();

		auto encoder = cmdbuf->computeCommandEncoder();

		encoder->setComputePipelineState(pso);
		encoder->setBuffer(bufA, 0, 0);
		encoder->setBuffer(bufB, 0, 1);
		encoder->setBuffer(bufResult, 0, 2);

		Indium::Size gridSize = Indium::Size { arrayLength, 1, 1 };

		size_t threadGroupSize = pso->maxTotalThreadsPerThreadgroup();
		if (threadGroupSize > arrayLength) {
			threadGroupSize = arrayLength;
		}
		Indium::Size threadgroupSize = Indium::Size { threadGroupSize, 1, 1 };

		encoder->dispatchThreads(gridSize, threadgroupSize);

		encoder->endEncoding();
		cmdbuf->commit();
		cmdbuf->waitUntilCompleted();

		auto a = static_cast<float*>(bufA->contents());
		auto b = static_cast<float*>(bufB->contents());
		auto result = static_cast<float*>(bufResult->contents());
		bool ok = true;
		for (size_t i = 0; i < arrayLength; ++i) {
			if (result[i] != (a[i] + b[i])) {
				std::cerr << "Compute ERROR: index=" << i << " result=" << result[i] << " vs " << (a[i] + b[i]) << "=a+b" << std::endl;
				ok = false;
			}
		}
		if (ok) {
			std::cout << "Compute results as expected" << std::endl;
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	std::cout << "Execution finished" << std::endl;

	return 0;
};
