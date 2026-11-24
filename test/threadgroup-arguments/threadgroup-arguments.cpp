// Copyright (C) 2026 Darling Developers
//
// Kernel arguments in threadgroup memory ([[threadgroup(n)]]), sized with setThreadgroupMemoryLength:
//  - a kernel that reverses its threadgroup through such an argument gets one as long as the length set, in a
//    direct and in an indirect dispatch, and with another length in a later dispatch of the same encoder;
//  - a kernel with two of them, of 16- and 4-byte elements, sizes each by its own index and element size;
//  - a reflection that gives the arguments unusable specialization IDs, indices or element sizes is refused.
// The module is mslc's output for threadgroup-arguments.metal in this directory.
// Prints SKIP and exits 0 without a Vulkan device.

#include "threadgroup-arguments-spv.h"

#include <indium/indium.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t noIndex = SIZE_MAX;

// what mslc's reflection says for the module
Indium::LibraryReflection moduleReflection() {
	Indium::LibraryReflection reflection;
	for (auto name: { "reverse", "pair" }) {
		auto& function = reflection.functions[name];
		function.functionType = Indium::FunctionType::Kernel;
		function.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
	}
	reflection.functions["reverse"].threadgroupArguments = { { 0, 3, 4 } };
	reflection.functions["pair"].threadgroupArguments = { { 1, 4, 16 }, { 0, 5, 4 } };
	return reflection;
}

}

int main() {
	Indium::init(nullptr, 0, false);

	{
		auto all = Indium::copyAllDevices();
		if (all.empty()) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}

		auto device = Indium::createSystemDefaultDevice();
		std::printf("device: %s\n", device->name().c_str());
		std::atomic<bool> keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});
		auto queue = device->newCommandQueue();

		std::string error;
		auto library = device->newLibrary(threadgroup_arguments_spv, threadgroup_arguments_spv_len, moduleReflection(), &error);
		if (!library) {
			std::printf("newLibrary: %s\n", error.c_str());
		}
		report(library != nullptr, "the module loads");
		if (library) {
			auto reverse = device->newComputePipelineState(library->newFunction("reverse"));
			auto pair = device->newComputePipelineState(library->newFunction("pair"));

			auto floats = [&](size_t count) {
				auto buffer = device->newBuffer(count * sizeof(float), Indium::ResourceOptions::StorageModeShared);
				auto values = static_cast<float*>(buffer->contents());
				for (size_t i = 0; i < count; ++i) {
					values[i] = float(i);
				}
				return buffer;
			};
			auto reversed = [](const std::shared_ptr<Indium::Buffer>& buffer, size_t count) {
				auto values = static_cast<const float*>(buffer->contents());
				for (size_t i = 0; i < count; ++i) {
					if (values[i] != float(count - 1 - i)) {
						return false;
					}
				}
				return true;
			};
			auto run = [&](const std::function<void(const std::shared_ptr<Indium::ComputeCommandEncoder>&)>& encode) {
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encode(encoder);
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
			};

			{
				auto x = floats(64);
				run([&](const auto& encoder) {
					encoder->setComputePipelineState(reverse);
					encoder->setBuffer(x, 0, 0);
					encoder->setThreadgroupMemoryLength(64 * sizeof(float), 0);
					encoder->dispatchThreadgroups({ 1, 1, 1 }, { 64, 1, 1 });
				});
				report(reversed(x, 64), "a threadgroup of 64 reverses through 256 bytes of threadgroup memory");
			}

			{
				// the length is the encoder's state, so it changes for the dispatches after it only
				auto first = floats(64);
				auto second = floats(16);
				run([&](const auto& encoder) {
					encoder->setComputePipelineState(reverse);
					encoder->setThreadgroupMemoryLength(64 * sizeof(float), 0);
					encoder->setBuffer(first, 0, 0);
					encoder->dispatchThreadgroups({ 1, 1, 1 }, { 64, 1, 1 });
					encoder->setThreadgroupMemoryLength(16 * sizeof(float), 0);
					encoder->setBuffer(second, 0, 0);
					encoder->dispatchThreadgroups({ 1, 1, 1 }, { 16, 1, 1 });
				});
				report(reversed(first, 64) && reversed(second, 16), "two dispatches of one encoder with different lengths");
			}

			{
				auto x = floats(32);
				auto arguments = device->newBuffer(3 * sizeof(uint32_t), Indium::ResourceOptions::StorageModeShared);
				auto words = static_cast<uint32_t*>(arguments->contents());
				words[0] = words[1] = words[2] = 1;
				run([&](const auto& encoder) {
					encoder->setComputePipelineState(reverse);
					encoder->setBuffer(x, 0, 0);
					encoder->setThreadgroupMemoryLength(32 * sizeof(float), 0);
					encoder->dispatchThreadgroups(arguments, 0, { 32, 1, 1 });
				});
				report(reversed(x, 32), "an indirect dispatch gets the length too");
			}

			{
				constexpr size_t n = 32;
				auto out = device->newBuffer(n * sizeof(uint32_t), Indium::ResourceOptions::StorageModeShared);
				run([&](const auto& encoder) {
					encoder->setComputePipelineState(pair);
					encoder->setBuffer(out, 0, 0);
					encoder->setThreadgroupMemoryLength(n * 4, 0);
					encoder->setThreadgroupMemoryLength(n * 16, 1);
					encoder->dispatchThreadgroups({ 1, 1, 1 }, { n, 1, 1 });
				});
				auto words = static_cast<const uint32_t*>(out->contents());
				bool ok = true;
				for (size_t i = 0; i < n; ++i) {
					ok = ok && words[i] == 11 * (n - 1 - i);
				}
				report(ok, "two arguments are sized by their own index and element size");
			}
		}

		// reflections that cannot describe the module's arguments
		auto refused = [&](const char* what, const std::function<void(Indium::LibraryReflection&)>& change) {
			auto reflection = moduleReflection();
			change(reflection);
			std::string error;
			auto library = device->newLibrary(threadgroup_arguments_spv, threadgroup_arguments_spv_len, reflection, &error);
			if (!library) {
				std::printf("     %s\n", error.c_str());
			}
			report(!library && !error.empty(), what);
		};
		refused("a specialization ID that carries the threadgroup size is refused", [](auto& reflection) {
			reflection.functions["reverse"].threadgroupArguments[0].specializationID = 1;
		});
		refused("two arguments with one specialization ID are refused", [](auto& reflection) {
			reflection.functions["pair"].threadgroupArguments[1].specializationID = 4;
		});
		refused("an argument and a grid size with one specialization ID are refused", [](auto& reflection) {
			reflection.functions["reverse"].gridSizeSpecializationIDs = std::array<uint32_t, 3> { 3, 6, 7 };
		});
		refused("two arguments with one index are refused", [](auto& reflection) {
			reflection.functions["pair"].threadgroupArguments[1].index = 1;
		});
		refused("an argument with elements of size 0 is refused", [](auto& reflection) {
			reflection.functions["reverse"].threadgroupArguments[0].elementSize = 0;
		});
		refused("an argument of a function that is not a kernel is refused", [](auto& reflection) {
			reflection.functions["reverse"].functionType = Indium::FunctionType::Vertex;
		});

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
