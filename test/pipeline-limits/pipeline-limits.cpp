// Copyright (C) 2026 Darling Developers
//
// The threadgroup size limits of compute pipelines made from pipeline-limits.metallib: the limit a kernel
// declares, the descriptor's maxTotalThreadsPerThreadgroup and its requiredThreadsPerThreadgroup.
// Prints SKIP and exits 0 without a Vulkan device.

#include "pipeline-limits.h"

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
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

void check(const std::string& what, const std::function<bool()>& body) {
	try {
		report(body(), what);
	} catch (const std::exception& e) {
		report(false, what + ": " + e.what());
	}
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
		std::atomic<bool> keepPollingDevice { true };
		std::thread devicePollingThread([&]() {
			// a bounded wait, so that a wakeup sent just before this poll cannot leave it waiting forever
			while (keepPollingDevice) {
				device->pollEvents(10 * 1000 * 1000);
			}
		});

		VkPhysicalDeviceProperties properties;
		Indium::DynamicVK::vkGetPhysicalDeviceProperties(std::static_pointer_cast<Indium::PrivateDevice>(device)->physicalDevice(), &properties);
		const size_t deviceLimit = std::min<size_t>(properties.limits.maxComputeWorkGroupInvocations, 1024);

		auto library = device->newLibrary(pipeline_limits_metallib, pipeline_limits_metallib_len);
		report(library != nullptr, "the library loads");

		auto pipeline = [&](const char* kernel, size_t maxTotal, Indium::Size required) {
			Indium::ComputePipelineDescriptor descriptor;
			descriptor.computeFunction = library->newFunction(kernel);
			descriptor.maxTotalThreadsPerThreadgroup = maxTotal;
			descriptor.requiredThreadsPerThreadgroup = required;
			return device->newComputePipelineState(descriptor, Indium::PipelineOption::None, nullptr);
		};
		auto isRequired = [](const std::shared_ptr<Indium::ComputePipelineState>& state, Indium::Size size) {
			auto required = state->requiredThreadsPerThreadgroup();
			return required.width == size.width && required.height == size.height && required.depth == size.depth;
		};
		auto rejects = [&](const char* kernel, Indium::Size required) {
			try {
				pipeline(kernel, 0, required);
			} catch (const std::invalid_argument& e) {
				std::printf("     %s\n", e.what());
				return true;
			}
			return false;
		};

		check("a kernel without a limit allows what the device does", [&] {
			auto state = pipeline("unlimited", 0, { 0, 0, 0 });
			std::printf("     maxTotalThreadsPerThreadgroup %zu, device %zu\n", state->maxTotalThreadsPerThreadgroup(), deviceLimit);
			return state->maxTotalThreadsPerThreadgroup() == deviceLimit && isRequired(state, { 0, 0, 0 });
		});

		check("a kernel's limit caps the pipeline's", [&] {
			auto state = pipeline("limited", 0, { 0, 0, 0 });
			std::printf("     maxTotalThreadsPerThreadgroup %zu\n", state->maxTotalThreadsPerThreadgroup());
			return state->maxTotalThreadsPerThreadgroup() == 64;
		});

		check("the lower of the kernel's and the descriptor's limits applies", [&] {
			return pipeline("limited", 32, { 0, 0, 0 })->maxTotalThreadsPerThreadgroup() == 32 &&
				pipeline("limited", 128, { 0, 0, 0 })->maxTotalThreadsPerThreadgroup() == 64;
		});

		check("a required size is reported and is the pipeline's limit", [&] {
			auto state = pipeline("unlimited", 0, { 8, 4, 1 });
			auto limited = pipeline("limited", 0, { 64, 1, 1 });
			return isRequired(state, { 8, 4, 1 }) && state->maxTotalThreadsPerThreadgroup() == 32 &&
				isRequired(limited, { 64, 1, 1 }) && limited->maxTotalThreadsPerThreadgroup() == 64;
		});

		check("a dispatch at the required size runs every thread", [&] {
			auto state = pipeline("unlimited", 0, { 8, 4, 1 });
			// two threadgroups of 8x4 side by side, a grid of 16x4: every thread writes its own word
			std::vector<uint32_t> poison(64, 0xA5A5A5A5u);
			auto buffer = device->newBuffer(poison.data(), poison.size() * 4, Indium::ResourceOptions::StorageModeShared);
			auto queue = device->newCommandQueue();
			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->computeCommandEncoder();
			encoder->setComputePipelineState(state);
			encoder->setBuffer(buffer, 0, 0);
			encoder->dispatchThreadgroups(Indium::Size { 2, 1, 1 }, Indium::Size { 8, 4, 1 });
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
			auto words = static_cast<const uint32_t*>(buffer->contents());
			for (uint32_t i = 0; i < 64; ++i) {
				if (words[i] != i) {
					return false;
				}
			}
			return true;
		});

		check("a kernel's limit above the device's fails", [&] {
			try {
				pipeline("too_large", 0, { 0, 0, 0 });
			} catch (const std::invalid_argument& e) {
				std::printf("     %s\n", e.what());
				return true;
			}
			return false;
		});

		check("a required size above the kernel's limit fails", [&] {
			return rejects("limited", { 16, 8, 1 });
		});

		check("a required size above the descriptor's limit fails", [&] {
			try {
				pipeline("unlimited", 16, { 32, 1, 1 });
			} catch (const std::invalid_argument& e) {
				return true;
			}
			return false;
		});

		check("a required size with a zero dimension fails", [&] {
			return rejects("unlimited", { 8, 0, 1 });
		});

		// devices allow fewer threads along z than in total; Vulkan requires at least 64 and 128
		const size_t maxDepth = properties.limits.maxComputeWorkGroupSize[2];
		if (maxDepth < deviceLimit) {
			check("a required size beyond the device's limit in one dimension fails", [&] {
				return rejects("unlimited", { 1, 1, maxDepth + 1 });
			});
		} else {
			std::printf("SKIP a required size beyond the device's limit in one dimension: %zu threads fit along z\n", maxDepth);
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
