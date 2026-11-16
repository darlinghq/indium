// Copyright (C) 2026 Lubos Dolezel
//
// dispatchThreads over a grid that is not a multiple of the threadgroup size:
//  - a kernel that skips the threads beyond the grid size (mslc --mask-grid-size) runs exactly the threads of the
//    grid, in one, two and three dimensions, and still runs a dispatch of whole threadgroups unchanged;
//  - a kernel that cannot skip them (it has a barrier) is refused rather than run over the cut threadgroups;
//  - setThreadgroupMemoryLength is accepted.
// The shaders are mslc's output for grid.metal and barrier.metal in this directory.
// Prints SKIP and exits 0 without a Vulkan device.

#include "grid-spv.h"
#include "barrier-spv.h"

#include <indium/indium.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t noIndex = SIZE_MAX;
constexpr uint32_t poison = 0xDEADBEEF;
constexpr uint32_t gridSizeID = 0x40000;

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
		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});
		auto queue = device->newCommandQueue();

		auto load = [&](const unsigned char* spirv, size_t length, std::initializer_list<const char*> names, bool masked) {
			Indium::LibraryReflection reflection;
			for (auto name : names) {
				auto& function = reflection.functions[name];
				function.functionType = Indium::FunctionType::Kernel;
				function.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
				// what mslc's reflection says for a kernel it masks
				if (masked) {
					function.gridSizeSpecializationIDs = std::array<uint32_t, 3> { gridSizeID, gridSizeID + 1, gridSizeID + 2 };
				}
			}
			std::string error;
			auto library = device->newLibrary(spirv, length, reflection, &error);
			if (!library) {
				std::printf("newLibrary: %s\n", error.c_str());
			}
			return library;
		};
		auto gridLibrary = load(grid_spv, grid_spv_len, { "mark1D", "mark2D", "mark3D" }, true);
		auto barrierLibrary = load(barrier_spv, barrier_spv_len, { "markBarrier" }, false);
		report(gridLibrary && barrierLibrary, "the modules load");
		if (gridLibrary && barrierLibrary) {
			constexpr size_t slots = 64;
			auto out = device->newBuffer(slots * sizeof(uint32_t), Indium::ResourceOptions::StorageModeShared);
			auto words = static_cast<uint32_t*>(out->contents());

			// the number of slots written, or -1 if one holds anything but the expected value or the poison
			auto run = [&](const std::shared_ptr<Indium::Library>& library, const char* name, Indium::Size grid, Indium::Size threadgroup, bool threads,
				const std::function<uint32_t(size_t)>& expected, size_t& written) {
				for (size_t i = 0; i < slots; ++i) {
					words[i] = poison;
				}
				auto pso = device->newComputePipelineState(library->newFunction(name));
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(pso);
				encoder->setBuffer(out, 0, 0);
				bool ok = true;
				try {
					if (threads) {
						encoder->dispatchThreads(grid, threadgroup);
					} else {
						encoder->dispatchThreadgroups(grid, threadgroup);
					}
				} catch (const std::runtime_error&) {
					ok = false;
				}
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				written = 0;
				for (size_t i = 0; i < slots; ++i) {
					if (words[i] == poison) {
						continue;
					}
					if (words[i] != expected(i)) {
						written = size_t(-1);
						return ok;
					}
					++written;
				}
				return ok;
			};

			size_t written = 0;
			// one dimension: ten threads in threadgroups of four
			bool ok = run(gridLibrary, "mark1D", { 10, 1, 1 }, { 4, 1, 1 }, true, [](size_t i) { return i + 1; }, written);
			report(ok && written == 10, "dispatchThreads of 10 threads in threadgroups of 4 runs exactly 10 threads");

			ok = run(gridLibrary, "mark1D", { 1, 1, 1 }, { 4, 1, 1 }, true, [](size_t i) { return i + 1; }, written);
			report(ok && written == 1, "dispatchThreads of one thread in a threadgroup of 4 runs one thread");

			ok = run(gridLibrary, "mark1D", { 12, 1, 1 }, { 4, 1, 1 }, true, [](size_t i) { return i + 1; }, written);
			report(ok && written == 12, "dispatchThreads of a whole number of threadgroups still runs every thread");

			ok = run(gridLibrary, "mark1D", { 7, 1, 1 }, { 7, 1, 1 }, true, [](size_t i) { return i + 1; }, written);
			report(ok && written == 7, "a grid that is one threadgroup is not cut");

			// whole threadgroups with dispatchThreadgroups keep every thread: the grid size constants are not set
			ok = run(gridLibrary, "mark1D", { 3, 1, 1 }, { 4, 1, 1 }, false, [](size_t i) { return i + 1; }, written);
			report(ok && written == 12, "dispatchThreadgroups runs every thread of every threadgroup");

			// two dimensions: 5 x 3 threads in threadgroups of 2 x 2
			ok = run(gridLibrary, "mark2D", { 5, 3, 1 }, { 2, 2, 1 }, true, [](size_t i) { return 1 + (i / 5) * 10 + i % 5; }, written);
			report(ok && written == 15, "dispatchThreads of 5 x 3 threads in threadgroups of 2 x 2 runs exactly 15 threads");

			// three dimensions: 5 x 3 x 2 threads in threadgroups of 2 x 2 x 2
			ok = run(gridLibrary, "mark3D", { 5, 3, 2 }, { 2, 2, 2 }, true, [](size_t i) { return 1 + (i / 15) * 100 + ((i / 5) % 3) * 10 + i % 5; }, written);
			report(ok && written == 30, "dispatchThreads of 5 x 3 x 2 threads in threadgroups of 2 x 2 x 2 runs exactly 30 threads");

			// a kernel that cannot skip threads
			ok = run(barrierLibrary, "markBarrier", { 8, 1, 1 }, { 4, 1, 1 }, true, [](size_t i) { return i + 1; }, written);
			report(ok && written == 8, "a kernel with a barrier runs a grid that is a whole number of threadgroups");
			ok = run(barrierLibrary, "markBarrier", { 10, 1, 1 }, { 4, 1, 1 }, true, [](size_t i) { return i + 1; }, written);
			report(!ok && written == 0, "a kernel with a barrier is refused for a grid that is cut and runs nothing");

			// setThreadgroupMemoryLength is accepted
			{
				auto pso = device->newComputePipelineState(gridLibrary->newFunction("mark1D"));
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				bool threw = false;
				try {
					encoder->setComputePipelineState(pso);
					encoder->setThreadgroupMemoryLength(256, 0);
					encoder->setThreadgroupMemoryLength(16, 3);
				} catch (const std::exception&) {
					threw = true;
				}
				report(!threw, "setThreadgroupMemoryLength does not throw");
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
			}
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
