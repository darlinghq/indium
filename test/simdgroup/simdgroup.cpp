// Copyright (C) 2026 Lubos Dolezel
//
// Kernels from simdgroup.metallib that use the simdgroup features of the Metal Shading Language: the position of a
// thread in its simdgroup, reductions, prefix scans, broadcasts, shuffles, votes, ballots and simdgroup_barrier().
// One threadgroup of 64 threads runs each kernel; the width of a simdgroup depends on the device, so the test
// asks the device for it first and compares every thread's result with what the specification defines for the
// threads of its simdgroup (shuffles reading outside the simdgroup have no defined result and are not compared).
// Prints SKIP and exits 0 without a Vulkan device.

#include "simdgroup.h"

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

constexpr size_t threads = 64;

using Words = std::vector<uint32_t>;

float asFloat(uint32_t word) {
	float value;
	std::memcpy(&value, &word, sizeof(value));
	return value;
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
		auto library = device->newLibrary(simdgroup_metallib, simdgroup_metallib_len);
		report(library != nullptr, "the library loads");

		// runs `kernel` over one threadgroup of `count` threads with buffers of the given sizes in words, and returns their contents
		auto run = [&](const char* kernel, std::vector<size_t> sizes, size_t count = threads) {
			auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
			if (!pipeline) {
				throw std::runtime_error(std::string(kernel) + " did not load");
			}
			std::vector<std::shared_ptr<Indium::Buffer>> bound;
			for (auto size: sizes) {
				Words poison(size, 0xA5A5A5A5u);
				bound.push_back(device->newBuffer(poison.data(), poison.size() * 4, Indium::ResourceOptions::StorageModeShared));
			}
			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->computeCommandEncoder();
			encoder->setComputePipelineState(pipeline);
			for (size_t i = 0; i < bound.size(); ++i) {
				encoder->setBuffer(bound[i], 0, i);
			}
			encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { count, 1, 1 });
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
			std::vector<Words> results;
			for (size_t i = 0; i < bound.size(); ++i) {
				auto words = static_cast<const uint32_t*>(bound[i]->contents());
				results.emplace_back(words, words + sizes[i]);
			}
			return results;
		};

		auto check = [&](const char* kernel, const std::string& what, const std::function<bool()>& body) {
			try {
				report(body(), std::string(kernel) + ": " + what);
			} catch (const std::exception& e) {
				report(false, std::string(kernel) + ": " + e.what());
			}
		};

		// the built-ins, and with them the width of a simdgroup
		size_t width = 0;
		check("simd_ids", "lane, simdgroup, number of simdgroups and width", [&] {
			auto out = run("simd_ids", { threads * 4 })[0];
			width = out[3];
			bool ok = width >= 4 && width <= 128 && threads % width == 0;
			for (size_t t = 0; ok && t < threads; ++t) {
				ok = out[t * 4] == t % width && out[t * 4 + 1] == t / width && out[t * 4 + 2] == threads / width && out[t * 4 + 3] == width;
			}
			return ok;
		});

		// 32 where the device can require it for compute, and the device's default otherwise
		check("simd_ids", "the width is 32 where the device can require it", [&] {
			VkPhysicalDeviceVulkan13Properties vk13 {};
			vk13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
			VkPhysicalDeviceVulkan11Properties vk11 {};
			vk11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
			vk11.pNext = &vk13;
			VkPhysicalDeviceProperties2 properties {};
			properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
			properties.pNext = &vk11;
			Indium::DynamicVK::vkGetPhysicalDeviceProperties2(std::static_pointer_cast<Indium::PrivateDevice>(device)->physicalDevice(), &properties);
			bool supported = vk13.minSubgroupSize <= 32 && 32 <= vk13.maxSubgroupSize && (vk13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
				32ull * vk13.maxComputeWorkgroupSubgroups >= std::min<uint32_t>(properties.properties.limits.maxComputeWorkGroupInvocations, 1024);
			size_t expected = supported ? 32 : vk11.subgroupSize;
			std::printf("     subgroup sizes %u to %u, default %u: expecting %zu\n", vk13.minSubgroupSize, vk13.maxSubgroupSize, vk11.subgroupSize, expected);
			auto pipeline = device->newComputePipelineState(library->newFunction("simd_ids"));
			return width == expected && pipeline->threadExecutionWidth() == expected;
		});

		// a threadgroup whose width is not a multiple of the SIMD-group's has a partial SIMD-group
		check("simd_ids", "33 threads run at the same width", [&] {
			constexpr size_t count = 33;
			auto out = run("simd_ids", { count * 4 }, count)[0];
			bool ok = true;
			for (size_t t = 0; t < count; ++t) {
				ok = ok && out[t * 4 + 3] == width;
			}
			return ok;
		});

		if (width == 0 || threads % width != 0) {
			std::printf("FAIL unusable simdgroup width %zu\n", width);
			failures++;
			width = 32;
		}
		std::printf("simdgroup width %zu\n", width);

		check("simd_ids", "the pipeline's limits: the width it ran at, Metal's thread limit and no threadgroup memory", [&] {
			VkPhysicalDeviceProperties properties;
			Indium::DynamicVK::vkGetPhysicalDeviceProperties(std::static_pointer_cast<Indium::PrivateDevice>(device)->physicalDevice(), &properties);
			auto pipeline = device->newComputePipelineState(library->newFunction("simd_ids"));
			std::printf("     maxTotalThreadsPerThreadgroup %zu, threadExecutionWidth %zu, staticThreadgroupMemoryLength %zu\n", pipeline->maxTotalThreadsPerThreadgroup(), pipeline->threadExecutionWidth(), pipeline->staticThreadgroupMemoryLength());
			Indium::ComputePipelineDescriptor descriptor;
			descriptor.computeFunction = library->newFunction("simd_ids");
			descriptor.maxTotalThreadsPerThreadgroup = 64;
			auto limited = device->newComputePipelineState(descriptor, Indium::PipelineOption::None, nullptr);
			return pipeline->maxTotalThreadsPerThreadgroup() == std::min<size_t>(properties.limits.maxComputeWorkGroupInvocations, 1024) &&
				pipeline->threadExecutionWidth() == width && pipeline->staticThreadgroupMemoryLength() == 0 &&
				limited->maxTotalThreadsPerThreadgroup() == 64;
		});

		check("simd_ids_short", "lane and width as 16-bit integers", [&] {
			auto out = run("simd_ids_short", { threads * 2 })[0];
			bool ok = true;
			for (size_t t = 0; t < threads; ++t) {
				ok = ok && out[t * 2] == t % width && out[t * 2 + 1] == width;
			}
			return ok;
		});

		// calls `expect(first, t)` for every thread t with the first thread of its simdgroup, and compares its words
		auto perThread = [&](const Words& out, size_t stride, const std::function<bool(size_t first, size_t t, const uint32_t* words)>& expect) {
			for (size_t t = 0; t < threads; ++t) {
				if (!expect(t - t % width, t, &out[t * stride])) {
					return false;
				}
			}
			return true;
		};

		check("simd_reduce", "sum, minimum, maximum, and, or and xor of unsigned values", [&] {
			auto out = run("simd_reduce", { threads * 6 })[0];
			return perThread(out, 6, [&](size_t first, size_t, const uint32_t* w) {
				uint32_t sum = 0, min = ~0u, max = 0, andAll = ~0u, orAll = 0, xorAll = 0;
				for (size_t i = first; i < first + width; ++i) {
					sum += i;
					min = std::min<uint32_t>(min, i + 3);
					max = std::max<uint32_t>(max, i);
					andAll &= i | 0xF0u;
					orAll |= i & 3u;
					xorAll ^= i;
				}
				return w[0] == sum && w[1] == min && w[2] == max && w[3] == andAll && w[4] == orAll && w[5] == xorAll;
			});
		});

		check("simd_reduce_float", "sum, product, minimum and maximum of floats", [&] {
			auto out = run("simd_reduce_float", { threads * 4 })[0];
			return perThread(out, 4, [&](size_t first, size_t, const uint32_t* w) {
				float sum = 0, product = 1, min = 1e9f, max = -1e9f;
				for (size_t i = first; i < first + width; ++i) {
					sum += i * 0.5f;
					product *= 1.0f + (i & 1);
					min = std::min(min, i - 4.0f);
					max = std::max(max, i * 2.0f);
				}
				return asFloat(w[0]) == sum && asFloat(w[1]) == product && asFloat(w[2]) == min && asFloat(w[3]) == max;
			});
		});

		check("simd_reduce_signed", "minimum and maximum of signed values", [&] {
			auto out = run("simd_reduce_signed", { threads * 2 })[0];
			return perThread(out, 2, [&](size_t first, size_t, const uint32_t* w) {
				return static_cast<int32_t>(w[0]) == static_cast<int>(first) - 20 && static_cast<int32_t>(w[1]) == static_cast<int>(first + width - 1) - 20;
			});
		});

		check("simd_prefix", "inclusive and exclusive prefix sums", [&] {
			auto results = run("simd_prefix", { threads * 2, threads });
			return perThread(results[0], 2, [&](size_t first, size_t t, const uint32_t* w) {
				uint32_t inclusive = 0;
				for (size_t i = first; i <= t; ++i) {
					inclusive += i;
				}
				float inclusiveFloat = 0;
				for (size_t i = first; i <= t; ++i) {
					inclusiveFloat += i + 0.5f;
				}
				return w[0] == inclusive && w[1] == inclusive - t && asFloat(results[1][t]) == inclusiveFloat;
			});
		});

		check("simd_broadcast_ops", "broadcast from a constant lane, from the first lane and from a computed lane", [&] {
			auto out = run("simd_broadcast_ops", { threads * 3 })[0];
			return perThread(out, 3, [&](size_t first, size_t, const uint32_t* w) {
				return w[0] == first + 3 && w[1] == first && w[2] == first + width - 1;
			});
		});

		check("simd_shuffle_ops", "shuffle, shuffle_xor, shuffle_down and shuffle_up", [&] {
			auto out = run("simd_shuffle_ops", { threads * 4 })[0];
			return perThread(out, 4, [&](size_t first, size_t t, const uint32_t* w) {
				size_t lane = t - first;
				bool ok = w[0] == first + (lane + 1) % width && w[1] == (t ^ 1);
				// shuffles that read outside the simdgroup have no defined result
				ok = ok && (lane + 1 >= width || w[2] == t + 1);
				ok = ok && (lane == 0 || w[3] == t - 1);
				return ok;
			});
		});

		check("simd_vote_ops", "all and any", [&] {
			auto out = run("simd_vote_ops", { threads * 4 })[0];
			return perThread(out, 4, [&](size_t, size_t, const uint32_t* w) {
				return w[0] == 1 && w[1] == 0 && w[2] == 1 && w[3] == 0;
			});
		});

		check("simd_ballot_ops", "ballot of the odd lanes and the mask of the active threads", [&] {
			auto out = run("simd_ballot_ops", { threads * 4 })[0];
			uint64_t odd = 0, all = 0;
			for (size_t lane = 0; lane < width && lane < 64; ++lane) {
				all |= 1ull << lane;
				odd |= (lane & 1) ? 1ull << lane : 0;
			}
			return perThread(out, 4, [&](size_t, size_t, const uint32_t* w) {
				return w[0] == uint32_t(odd) && w[1] == uint32_t(odd >> 32) && w[2] == uint32_t(all) && w[3] == uint32_t(all >> 32);
			});
		});

		check("simd_barriers", "stores around simdgroup barriers all happen", [&] {
			auto out = run("simd_barriers", { threads * 3 })[0];
			return perThread(out, 3, [&](size_t, size_t, const uint32_t* w) {
				return w[0] == 1 && w[1] == 2 && w[2] == 3;
			});
		});

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
