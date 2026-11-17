// Copyright (C) 2026 Lubos Dolezel
//
// Kernels from threadgroup.metallib that use threadgroup memory and threadgroup_barrier(): a tile reversed
// through threadgroup memory, a tree reduction (a loop with a barrier in it) and a value one thread
// computes for the whole group. The Metal Shading Language specification defines the results; every one
// is compared with the same computation on the host.
// Prints SKIP and exits 0 without a Vulkan device.

#include "threadgroup.h"

#include <indium/indium.hpp>

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
std::vector<T> run(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, const std::vector<T>& input, size_t outputCount, size_t groups, size_t groupSize) {
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
	encoder->dispatchThreadgroups(Indium::Size { groups, 1, 1 }, Indium::Size { groupSize, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	std::vector<T> result(outputCount);
	std::memcpy(result.data(), out->contents(), result.size() * sizeof(T));
	return result;
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
		auto library = device->newLibrary(threadgroup_metallib, threadgroup_metallib_len);
		report(library != nullptr, "the library loads");

		try {
			// every group reverses its own 64 elements
			constexpr size_t groups = 5, size = 64;
			std::vector<uint32_t> input(groups * size);
			for (size_t i = 0; i < input.size(); ++i) {
				input[i] = static_cast<uint32_t>(i * 7 + 3);
			}
			std::vector<uint32_t> want(input.size());
			for (size_t g = 0; g < groups; ++g) {
				for (size_t l = 0; l < size; ++l) {
					want[g * size + l] = input[g * size + size - 1 - l];
				}
			}
			report(run(device.get(), queue.get(), library.get(), "reverse_tile", input, input.size(), groups, size) == want, "reverse_tile: each thread reads what another thread of its group stored");

			// each group's sum (the kernel halves the stride with a barrier per step)
			std::vector<uint32_t> sums(groups, 0);
			for (size_t i = 0; i < input.size(); ++i) {
				sums[i / size] += input[i];
			}
			report(run(device.get(), queue.get(), library.get(), "sum_tile", input, groups, groups, size) == sums, "sum_tile: a reduction with a barrier inside the loop");

			// 32 threads per group; each group's total of x + 2x over its 32 inputs
			constexpr size_t floatGroups = 3, floatSize = 32;
			std::vector<float> floats(floatGroups * floatSize);
			for (size_t i = 0; i < floats.size(); ++i) {
				floats[i] = static_cast<float>(i) * 0.5f;
			}
			std::vector<float> floatWant(floats.size());
			for (size_t g = 0; g < floatGroups; ++g) {
				float total = 0;
				for (size_t l = 0; l < floatSize; ++l) {
					total += floats[g * floatSize + l] * 3.0f;
				}
				for (size_t l = 0; l < floatSize; ++l) {
					floatWant[g * floatSize + l] = total;
				}
			}
			report(run(device.get(), queue.get(), library.get(), "float_tile", floats, floats.size(), floatGroups, floatSize) == floatWant, "float_tile: threadgroup vectors and a scalar, written by one thread and read by all");

			// the position, size and count built-ins of a three-group, sixteen-thread dispatch
			{
				constexpr size_t builtinGroups = 3, builtinSize = 16;
				std::vector<uint32_t> expected;
				for (size_t g = 0; g < builtinGroups; ++g) {
					for (size_t l = 0; l < builtinSize; ++l) {
						expected.insert(expected.end(), { uint32_t(l), uint32_t(g), builtinSize, builtinGroups, builtinGroups * builtinSize, uint32_t(l), 0, 101 });
					}
				}
				report(run(device.get(), queue.get(), library.get(), "builtins", std::vector<uint32_t>(4, 0), expected.size(), builtinGroups, builtinSize) == expected, "builtins: thread and threadgroup positions, sizes and counts");
			}

			// the groups do not share threadgroup memory: run the reversal again with other data
			for (auto& value: input) {
				value = ~value;
			}
			for (size_t g = 0; g < groups; ++g) {
				for (size_t l = 0; l < size; ++l) {
					want[g * size + l] = input[g * size + size - 1 - l];
				}
			}
			report(run(device.get(), queue.get(), library.get(), "reverse_tile", input, input.size(), groups, size) == want, "reverse_tile: a second dispatch is independent of the first");
		} catch (const std::exception& e) {
			report(false, e.what());
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
