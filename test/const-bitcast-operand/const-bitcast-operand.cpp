#include "const-bitcast-operand.h"

#include <indium/indium.hpp>

#include <cstring>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

// One float4 each way, so a readback of 16 bytes is the whole shader.
static constexpr size_t vectorSize = 16;
static constexpr unsigned char poison = 0xA5;

static size_t differingBytes(const void* got, const void* want, size_t size) {
	auto a = static_cast<const unsigned char*>(got);
	auto b = static_cast<const unsigned char*>(want);
	size_t count = 0;

	for (size_t i = 0; i < size; ++i) {
		if (a[i] != b[i]) {
			++count;
		}
	}

	return count;
}

static std::vector<unsigned char> run(Indium::Device* device, const float (&src)[4]) {
	// The destination starts as poison, so a store that never happens is a
	// mismatch rather than a match.
	std::vector<unsigned char> poisonBuffer(vectorSize, poison);

	auto dstBuffer = device->newBuffer(poisonBuffer.data(), vectorSize, Indium::ResourceOptions::StorageModeShared);
	auto srcBuffer = device->newBuffer(&src, sizeof(src), Indium::ResourceOptions::StorageModeShared);

	// Translation happens here, so a module the translator cannot make sense of
	// fails before any dispatch rather than passing silently.
	auto lib = device->newLibrary(const_bitcast_operand, const_bitcast_operand_len);
	auto pso = device->newComputePipelineState(lib->newFunction("constant_bitcast_operand"));
	auto commandQueue = device->newCommandQueue();

	auto cmdbuf = commandQueue->commandBuffer();
	auto encoder = cmdbuf->computeCommandEncoder();

	encoder->setComputePipelineState(pso);
	encoder->setBuffer(dstBuffer, 0, 0);
	encoder->setBuffer(srcBuffer, 0, 1);
	encoder->dispatchThreads(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });

	encoder->endEncoding();
	cmdbuf->commit();
	cmdbuf->waitUntilCompleted();

	std::vector<unsigned char> result(vectorSize);
	std::memcpy(result.data(), dstBuffer->contents(), vectorSize);

	return result;
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

		// All four floats are distinct, so a readback of the wrong bytes, or of
		// the right bytes in the wrong order, shows up as a difference rather
		// than coinciding with the expectation.
		float src[4] { 1.0f, 2.5f, 4.25f, 6.125f };

		auto result = run(device.get(), src);
		auto bad = differingBytes(result.data(), &src, vectorSize);

		if (bad == 0) {
			std::cout << "A module carrying a bitcast of a constant still writes what shadersrc/const-bitcast-operand.metal says" << std::endl;
		} else {
			std::cerr << "Readback ERROR: " << bad << " of " << vectorSize
				<< " bytes differ from the source" << std::endl;
			ok = false;
		}

		// Negative control. Reversing the source changes what the shader must
		// write, so the readback has to change too. A comparison that passed
		// regardless of the bytes read would fail here.
		float permuted[4] { src[3], src[2], src[1], src[0] };

		auto permutedResult = run(device.get(), permuted);
		size_t changed = 0;
		for (size_t i = 0; i < vectorSize; ++i) {
			if (result[i] != permutedResult[i]) {
				++changed;
			}
		}

		std::cout << "Negative control: " << changed << " of " << vectorSize
			<< " bytes changed when the source floats were reversed" << std::endl;

		if (changed == 0) {
			std::cerr << "Control ERROR: the readback does not depend on the source bytes" << std::endl;
			ok = false;
		}

		if (differingBytes(permutedResult.data(), &permuted, vectorSize) != 0) {
			std::cerr << "Control ERROR: the permuted run does not match its own expectation" << std::endl;
			ok = false;
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	std::cout << "Execution finished" << std::endl;

	return ok ? 0 : 1;
};
