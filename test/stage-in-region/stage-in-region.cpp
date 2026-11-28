// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in stage-in-region.metallib, which read the stage-in region (MSL's [[grid_origin]] and
// [[grid_size]]) as 3 and 2-component vectors and scalars: without a region, after setStageInRegion(), in a later
// dispatch of the same encoder, and in a new encoder, which starts without a region again.
// Prints SKIP and exits 0 without a Vulkan device.

#include "stage-in-region.h"

#include <indium/indium.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
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

constexpr size_t threads = 4;

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
		// the bounded wait lets the thread see the flag even if the wakeup comes before it waits
		std::atomic<bool> keepPollingDevice { true };
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(100000000);
			}
		});
		auto queue = device->newCommandQueue();
		auto library = device->newLibrary(stage_in_region_metallib, stage_in_region_metallib_len);
		report(library != nullptr, "the library loads");

		const Indium::Region region { Indium::Origin { 3, 5, 7 }, Indium::Size { 11, 13, 17 } };
		const Indium::Region other { Indium::Origin { 2, 4, 6 }, Indium::Size { 8, 10, 12 } };

		// `kernel` writes `slots` words per thread: the origin's and then the size's components
		auto check = [&](const char* kernel, size_t slots) {
			try {
				auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
				if (!pipeline) {
					throw std::runtime_error("the pipeline did not load");
				}
				// four dispatches: none with a region, then `region`, then one more in the same encoder, then
				// `other` in the same encoder; a fifth in a new encoder
				std::vector<std::shared_ptr<Indium::Buffer>> outs;
				for (size_t i = 0; i < 5; ++i) {
					std::vector<uint32_t> poison(threads * slots, 0xA5A5A5A5u);
					outs.push_back(device->newBuffer(poison.data(), poison.size() * sizeof(uint32_t), Indium::ResourceOptions::StorageModeShared));
				}
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(pipeline);
				auto dispatch = [&](size_t i) {
					encoder->setBuffer(outs[i], 0, 0);
					encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { threads, 1, 1 });
				};
				dispatch(0);
				encoder->setStageInRegion(region);
				dispatch(1);
				dispatch(2);
				encoder->setStageInRegion(other);
				dispatch(3);
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();

				commandBuffer = queue->commandBuffer();
				encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(pipeline);
				dispatch(4);
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();

				// the components the kernel reads of a region
				auto words = [&](const std::optional<Indium::Region>& r) {
					size_t origin[3] = { r ? r->origin.x : 0, r ? r->origin.y : 0, r ? r->origin.z : 0 };
					size_t size[3] = { r ? r->size.width : 0, r ? r->size.height : 0, r ? r->size.depth : 0 };
					std::vector<uint32_t> want;
					for (size_t k = 0; k < slots / 2; ++k) {
						want.push_back(origin[k]);
					}
					for (size_t k = 0; k < slots / 2; ++k) {
						want.push_back(size[k]);
					}
					return want;
				};
				const std::optional<Indium::Region> expected[5] = { std::nullopt, region, region, other, std::nullopt };
				bool ok = true;
				for (size_t i = 0; i < 5; ++i) {
					auto want = words(expected[i]);
					auto got = static_cast<const uint32_t*>(outs[i]->contents());
					for (size_t t = 0; t < threads; ++t) {
						for (size_t k = 0; k < slots; ++k) {
							if (got[t * slots + k] != want[k]) {
								std::printf("  %s: dispatch %zu thread %zu slot %zu: got 0x%08x, expected 0x%08x\n", kernel, i, t, k, got[t * slots + k], want[k]);
								ok = false;
							}
						}
					}
				}
				report(ok, std::string(kernel) + ": no region, a region, the same in the next dispatch, another, none in a new encoder");
			} catch (const std::exception& e) {
				report(false, std::string(kernel) + ": " + e.what());
			}
		};

		if (library) {
			check("region", 6);
			check("region_2d", 4);
			check("region_1d", 2);
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
