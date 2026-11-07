// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in air-ops.metallib on the system default device and compares every result with
// what the Metal Shading Language specification defines for the operation (computed here on the host).

#include "air-ops.h"

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

static constexpr size_t threadCount = 8;

template<typename T>
static std::vector<T> run(Indium::Device* device, const char* name, size_t outputCount, const std::vector<std::vector<T>>& inputs) {
	std::vector<T> poison(outputCount);
	std::memset(poison.data(), 0xA5, poison.size() * sizeof(T));

	auto library = device->newLibrary(air_ops_metallib, air_ops_metallib_len);
	auto pipeline = device->newComputePipelineState(library->newFunction(name));
	auto queue = device->newCommandQueue();
	auto output = device->newBuffer(poison.data(), poison.size() * sizeof(T), Indium::ResourceOptions::StorageModeShared);

	std::vector<std::shared_ptr<Indium::Buffer>> inputBuffers;
	for (auto& input: inputs) {
		inputBuffers.push_back(device->newBuffer(input.data(), input.size() * sizeof(T), Indium::ResourceOptions::StorageModeShared));
	}

	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(output, 0, 0);
	for (size_t i = 0; i < inputBuffers.size(); ++i) {
		encoder->setBuffer(inputBuffers[i], 0, i + 1);
	}
	encoder->dispatchThreads(Indium::Size { threadCount, 1, 1 }, Indium::Size { threadCount, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();

	std::vector<T> result(outputCount);
	std::memcpy(result.data(), output->contents(), result.size() * sizeof(T));
	return result;
}

static bool compare(const char* kernel, size_t slots, const std::vector<int32_t>& got, const std::vector<int32_t>& want) {
	size_t bad = 0;
	for (size_t i = 0; i < want.size(); ++i) {
		if (got[i] != want[i]) {
			if (bad++ < 16) {
				std::cerr << kernel << ": thread " << i / slots << " slot " << i % slots << ": got " << got[i] << ", expected " << want[i] << std::endl;
			}
		}
	}
	std::cout << kernel << ": " << (want.size() - bad) << " of " << want.size() << " results correct" << std::endl;
	return bad == 0;
}

static bool compare(const char* kernel, size_t slots, const std::vector<float>& got, const std::vector<float>& want) {
	size_t bad = 0;
	for (size_t i = 0; i < want.size(); ++i) {
		if (!(std::fabs(got[i] - want[i]) <= 1e-5f * std::fmax(1.0f, std::fabs(want[i])))) {
			if (bad++ < 16) {
				std::cerr << kernel << ": thread " << i / slots << " slot " << i % slots << ": got " << got[i] << ", expected " << want[i] << std::endl;
			}
		}
	}
	std::cout << kernel << ": " << (want.size() - bad) << " of " << want.size() << " results correct" << std::endl;
	return bad == 0;
}

static bool testIntOps(Indium::Device* device) {
	constexpr size_t slots = 18;
	std::vector<int32_t> a { 7, -7, 100, -100, 0, 123456, -1, 70000 };
	std::vector<int32_t> b { 3, 3, -7, -7, 9, 1000, 2, -1 };

	std::vector<int32_t> want;
	for (size_t i = 0; i < threadCount; ++i) {
		uint32_t ua = a[i], ub = b[i];
		uint32_t shift = b[i] & 7;
		want.push_back(ua / ub);
		want.push_back(ua % ub);
		want.push_back(a[i] / b[i]);
		want.push_back(a[i] % b[i]);
		want.push_back(ua << shift);
		want.push_back(ua >> shift);
		want.push_back(a[i] >> shift);
		want.push_back(a[i] & b[i]);
		want.push_back(a[i] | b[i]);
		want.push_back(a[i] ^ b[i]);
		want.push_back(a[i] < b[i] ? 1 : 0);
		want.push_back(ua < ub ? a[i] : b[i]);
		want.push_back(std::min(a[i], b[i]));
		want.push_back(std::max(ua, ub));
		want.push_back(std::abs(a[i]));
		want.push_back(static_cast<int16_t>(a[i]));
		want.push_back(std::min(std::max(a[i], -5), 5));
		want.push_back(static_cast<uint8_t>(a[i]));
	}

	return compare("int_ops", slots, run<int32_t>(device, "int_ops", want.size(), { a, b }), want);
}

static bool testFloatOps(Indium::Device* device) {
	constexpr size_t slots = 16;
	std::vector<float> x { 1.5f, -2.25f, 3.75f, -0.5f, 10.0f, 0.0f, 7.125f, -9.5f };
	std::vector<float> y { 0.5f, 1.0f, -2.0f, -0.5f, 3.0f, 1.0f, 2.5f, 4.0f };

	std::vector<float> want;
	for (size_t i = 0; i < threadCount; ++i) {
		float ax = std::fabs(x[i]);
		int32_t si = static_cast<int32_t>(x[i]);
		want.push_back(-x[i]);
		want.push_back(std::fmod(x[i], y[i]));
		want.push_back(std::sqrt(ax));
		want.push_back(std::floor(x[i]));
		want.push_back(std::fmin(x[i], y[i]));
		want.push_back(x[i] * y[i] + 1.0f);
		want.push_back(static_cast<float>(si));
		want.push_back(static_cast<float>(static_cast<uint32_t>(ax)));
		want.push_back(x[i] >= y[i] ? 1.0f : 0.0f);
		want.push_back(x[i] <= y[i] ? 1.0f : 0.0f);
		want.push_back(x[i] != y[i] ? 1.0f : 0.0f);
		want.push_back(x[i] == y[i] ? 1.0f : 0.0f);
		want.push_back(static_cast<float>(si * 2));
		want.push_back(static_cast<float>(static_cast<uint32_t>(ax)));
		want.push_back(std::fmin(std::fmax(x[i], -1.0f), 1.0f));
		want.push_back(ax - std::floor(ax));
	}

	return compare("float_ops", slots, run<float>(device, "float_ops", want.size(), { x, y }), want);
}

static bool testTableLookup(Indium::Device* device) {
	std::vector<float> want;
	for (size_t i = 0; i < threadCount; ++i) {
		want.push_back(static_cast<float>(1u << i));
	}
	return compare("table_lookup", 1, run<float>(device, "table_lookup", want.size(), {}), want);
}

static bool testControlFlow(Indium::Device* device) {
	constexpr size_t slots = 4;
	std::vector<int32_t> ns { 0, 1, 2, 5, 6, 9, 3, 12 };

	std::vector<int32_t> want;
	for (size_t id = 0; id < threadCount; ++id) {
		uint32_t n = ns[id];
		uint32_t squares = 0, nested = 0, k = 0;
		for (uint32_t i = 0; i < n; ++i) {
			squares += i * i;
			for (uint32_t j = 0; j < i; ++j) {
				nested += i ^ j;
			}
		}
		for (; k < n && k * k <= id * 5; ++k);
		want.push_back(squares);
		want.push_back((n & 1) ? n * 3 + 1 : n / 2);
		want.push_back(nested);
		want.push_back(k);
	}

	return compare("control_flow", slots, run<int32_t>(device, "control_flow", want.size(), { ns }), want);
}

// A buffer the kernel reads but the caller never bound must not fault the GPU. Metal leaves such a
// read undefined; Indium reads zeros, so every count is 0 and so is every result.
static bool testUnboundBuffer(Indium::Device* device) {
	std::vector<int32_t> want(threadCount * 4, 0);
	return compare("control_flow (unbound input)", 4, run<int32_t>(device, "control_flow", want.size(), {}), want);
}

// The Vulkan specification requires vkAllocateDescriptorSets to fail with VK_ERROR_OUT_OF_POOL_MEMORY
// once a pool's maxSets would be exceeded, but not every driver does (lavapipe does not), so this
// enforces it for the pools Indium creates (64 sets each).
static PFN_vkAllocateDescriptorSets originalAllocateDescriptorSets;
static std::mutex poolUseMutex;
static std::map<VkDescriptorPool, uint32_t> poolUse;

static VKAPI_ATTR VkResult VKAPI_CALL allocateWithinMaxSets(VkDevice device, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* sets) {
	{
		std::scoped_lock lock(poolUseMutex);
		auto& used = poolUse[info->descriptorPool];
		if (used + info->descriptorSetCount > 64) {
			return VK_ERROR_OUT_OF_POOL_MEMORY;
		}
		used += info->descriptorSetCount;
	}
	return originalAllocateDescriptorSets(device, info, sets);
}

// One encoder may encode any number of dispatches, each of which needs its own descriptor sets, and
// several threads may create and commit command buffers on one queue at once.
static bool testManyDispatches(Indium::Device* device) {
	std::vector<int32_t> ns { 0, 1, 2, 5, 6, 9, 3, 12 };
	auto want = run<int32_t>(device, "control_flow", threadCount * 4, { ns });

	auto library = device->newLibrary(air_ops_metallib, air_ops_metallib_len);
	auto pipeline = device->newComputePipelineState(library->newFunction("control_flow"));
	auto queue = device->newCommandQueue();
	auto input = device->newBuffer(ns.data(), ns.size() * sizeof(int32_t), Indium::ResourceOptions::StorageModeShared);

	auto encodeAndCheck = [&](size_t dispatches) {
		std::vector<std::shared_ptr<Indium::Buffer>> outputs;
		auto commandBuffer = queue->commandBuffer();
		auto encoder = commandBuffer->computeCommandEncoder();
		encoder->setComputePipelineState(pipeline);
		encoder->setBuffer(input, 0, 1);
		for (size_t i = 0; i < dispatches; ++i) {
			outputs.push_back(device->newBuffer(want.size() * sizeof(int32_t), Indium::ResourceOptions::StorageModeShared));
			encoder->setBuffer(outputs.back(), 0, 0);
			encoder->dispatchThreads(Indium::Size { threadCount, 1, 1 }, Indium::Size { threadCount, 1, 1 });
		}
		encoder->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		for (auto& output: outputs) {
			if (std::memcmp(output->contents(), want.data(), want.size() * sizeof(int32_t)) != 0) {
				return false;
			}
		}
		return true;
	};

	Indium::DynamicVK::vkAllocateDescriptorSets.resolve();
	originalAllocateDescriptorSets = reinterpret_cast<PFN_vkAllocateDescriptorSets>(Indium::DynamicVK::vkAllocateDescriptorSets.pointer);
	Indium::DynamicVK::vkAllocateDescriptorSets.pointer = reinterpret_cast<void*>(allocateWithinMaxSets);
	bool many = encodeAndCheck(300);
	Indium::DynamicVK::vkAllocateDescriptorSets.pointer = reinterpret_cast<void*>(originalAllocateDescriptorSets);
	std::cout << "300 dispatches in one encoder: " << (many ? "as expected" : "WRONG") << std::endl;

	std::atomic<bool> concurrent { true };
	std::vector<std::thread> threads;
	for (size_t t = 0; t < 4; ++t) {
		threads.emplace_back([&]() {
			for (size_t i = 0; i < 25; ++i) {
				if (!encodeAndCheck(2)) {
					concurrent = false;
				}
			}
		});
	}
	for (auto& thread: threads) {
		thread.join();
	}
	std::cout << "command buffers from 4 threads: " << (concurrent ? "as expected" : "WRONG") << std::endl;

	return many && concurrent;
}

// The library reports each kernel's buffer arguments as air-ops.metal declares them, and a pipeline
// asking for that reflection can be created.
static bool testReflection(Indium::Device* device) {
	auto library = device->newLibrary(air_ops_metallib, air_ops_metallib_len);
	auto function = library->newFunction("int_ops");
	bool ok = function->functionType() == Indium::FunctionType::Kernel;

	const char* names[] = { "out", "a", "b" };
	auto& arguments = function->argumentInfo();
	ok = ok && arguments.size() == 3;
	for (size_t i = 0; ok && i < arguments.size(); ++i) {
		auto& argument = arguments[i];
		ok = argument.kind == Indium::ArgumentKind::Buffer && argument.index == i && argument.name == names[i]
			&& argument.typeName == "int" && argument.size == 4 && argument.alignment == 4 && argument.members.empty();
	}

	try {
		device->newComputePipelineState(function, Indium::PipelineOption::ArgumentInfo | Indium::PipelineOption::BufferTypeInfo);
	} catch (const std::exception& e) {
		std::cerr << "reflection: pipeline creation failed: " << e.what() << std::endl;
		ok = false;
	}

	std::cout << "reflection: " << (ok ? "as declared" : "WRONG") << std::endl;
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

		ok = testIntOps(device.get()) && ok;
		ok = testFloatOps(device.get()) && ok;
		ok = testTableLookup(device.get()) && ok;
		ok = testControlFlow(device.get()) && ok;
		ok = testReflection(device.get()) && ok;
		ok = testUnboundBuffer(device.get()) && ok;
		ok = testManyDispatches(device.get()) && ok;

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	std::cout << (ok ? "All results as expected" : "Some results were wrong") << std::endl;

	return ok ? 0 : 1;
};
