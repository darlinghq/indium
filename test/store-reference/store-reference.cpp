#include "store-reference.h"

#include <indium/indium.hpp>

#include <cstring>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

// float4 color at 0, then three 16-byte float3x3 columns at 16, 32 and 48.
static constexpr size_t structSize = 64;
static constexpr size_t columnStride = 16;
static constexpr unsigned char poison = 0xA5;

struct Uniforms {
	float color[4];
	float basis[3][4];
};

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

static std::vector<unsigned char> writeColumns(Indium::Device* device, const Uniforms& src) {
	std::vector<unsigned char> poisonBuffer(structSize, poison);

	auto dstBuffer = device->newBuffer(poisonBuffer.data(), structSize, Indium::ResourceOptions::StorageModeShared);
	auto srcBuffer = device->newBuffer(&src, sizeof(src), Indium::ResourceOptions::StorageModeShared);

	auto lib = device->newLibrary(store_reference, store_reference_len);
	auto pso = device->newComputePipelineState(lib->newFunction("write_columns"));
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

	std::vector<unsigned char> result(structSize);
	std::memcpy(result.data(), dstBuffer->contents(), structSize);

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

		// Every one of the 16 floats is distinct, so a store landing at the
		// wrong offset, or writing only the first component of a column, shows
		// up as a difference rather than coinciding with the expectation.
		Uniforms src {};
		float value = 1.0f;
		for (size_t i = 0; i < 4; ++i) {
			src.color[i] = value;
			value += 1.5f;
		}
		for (size_t c = 0; c < 3; ++c) {
			for (size_t r = 0; r < 4; ++r) {
				src.basis[c][r] = value;
				value += 2.25f;
			}
		}

		// The destination starts as poison, so a store that never happens is a
		// mismatch rather than a match.
		auto result = writeColumns(device.get(), src);
		auto bad = differingBytes(result.data(), &src, structSize);

		if (bad == 0) {
			std::cout << "Every store landed at the offset shadersrc/store-reference.metal writes it to" << std::endl;
		} else {
			std::cerr << "Store ERROR: " << bad << " of " << structSize
				<< " bytes differ from what the shader should write" << std::endl;
			ok = false;
		}

		for (size_t c = 0; c < 3; ++c) {
			size_t offset = 16 + c * columnStride;
			bool exact = true;
			for (size_t r = 0; r < 4; ++r) {
				auto got = result[offset + r * 4];
				auto want = reinterpret_cast<const unsigned char*>(&src)[offset + r * 4];
				if (got != want) {
					exact = false;
				}
			}
			std::cout << "  column " << c << " at offset " << offset << ": "
				<< (exact ? "exact" : "MISMATCH") << std::endl;
		}

		// Negative control. Swapping color and basis[0] in the source changes
		// what the shader must write, so the readback has to change too. A
		// comparison that passed regardless of the bytes read would fail here.
		Uniforms permuted = src;
		for (size_t i = 0; i < 4; ++i) {
			permuted.color[i] = src.basis[0][i];
			permuted.basis[0][i] = src.color[i];
		}

		auto permutedResult = writeColumns(device.get(), permuted);
		size_t changed = 0;
		for (size_t i = 0; i < structSize; ++i) {
			if (result[i] != permutedResult[i]) {
				++changed;
			}
		}

		std::cout << "Negative control: " << changed << " of " << structSize
			<< " bytes changed when color and basis[0] were swapped in the source buffer" << std::endl;

		if (changed == 0) {
			std::cerr << "Control ERROR: the readback does not depend on the source bytes" << std::endl;
			ok = false;
		}

		if (differingBytes(permutedResult.data(), &permuted, structSize) != 0) {
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
