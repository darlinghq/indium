// Copyright (C) 2026 Lubos Dolezel
//
// A kernel that only reads its textures (TextureAccessType::Fetch) reads them by integer coordinate at a level
// chosen at run time, so they are bound as sampled images of every level rather than as storage images, whose
// views have one level. A cube is bound as the 2D array of its faces, as an image fetch takes no cube image:
//  - level 0, 1 and 2 of a mipmapped R32Float 2D texture read back their own values;
//  - each face of a mipmapped cube reads back its own value at level 1, in Metal's face order;
//  - the size queries see the size of the level asked for.
// The shader is mslc's output for fetch.metal in this directory. Prints SKIP and exits 0 without a Vulkan device.

#include "fetch-spv.h"

#include <indium/indium.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t noIndex = SIZE_MAX;
constexpr size_t outputs = 10;

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

		using Indium::PixelFormat;
		// 4x4 with levels 0 to 2, level L holding 10 + L
		auto flatDescriptor = Indium::TextureDescriptor::texture2DDescriptor(PixelFormat::R32Float, 4, 4, true);
		flatDescriptor.usage = Indium::TextureUsage::ShaderRead;
		auto flat = device->newTexture(flatDescriptor);
		for (size_t level = 0; level < 3; ++level) {
			size_t side = 4 >> level;
			std::vector<float> texels(side * side, 10.0f + level);
			flat->replaceRegion(Indium::Region::make2D(0, 0, side, side), level, texels.data(), side * sizeof(float));
		}
		// 2x2 faces with levels 0 and 1, face F at level L holding 100 + 10 * F + L
		auto cubeDescriptor = Indium::TextureDescriptor::textureCubeDescriptor(PixelFormat::R32Float, 2, true);
		cubeDescriptor.usage = Indium::TextureUsage::ShaderRead;
		auto cube = device->newTexture(cubeDescriptor);
		for (size_t face = 0; face < 6; ++face) {
			for (size_t level = 0; level < 2; ++level) {
				size_t side = 2 >> level;
				std::vector<float> texels(side * side, 100.0f + 10.0f * face + level);
				cube->replaceRegion(Indium::Region::make2D(0, 0, side, side), level, face, texels.data(),
					side * sizeof(float), side * side * sizeof(float));
			}
		}

		Indium::LibraryReflection reflection;
		auto& kernel = reflection.functions["fetchLevels"];
		kernel.functionType = Indium::FunctionType::Kernel;
		kernel.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
		kernel.bindings.push_back(Indium::BindingDescriptor { 1, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
		kernel.bindings.push_back(Indium::BindingDescriptor { 0, 1, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Fetch });
		kernel.bindings.push_back(Indium::BindingDescriptor { 1, 2, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Fetch });
		std::string error;
		auto library = device->newLibrary(fetch_spv, fetch_spv_len, reflection, &error);
		report(library != nullptr, "the module loads");
		if (library) {
			auto pipeline = device->newComputePipelineState(library->newFunction("fetchLevels"));
			report(pipeline != nullptr, "the kernel loads");
			auto out = device->newBuffer(outputs * 4 * sizeof(float), Indium::ResourceOptions::CPUCacheModeDefaultCache);
			std::memset(out->contents(), 0, outputs * 4 * sizeof(float));
			uint32_t cubeLevel = 1;
			auto level = device->newBuffer(&cubeLevel, sizeof(cubeLevel), Indium::ResourceOptions::CPUCacheModeDefaultCache);

			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->computeCommandEncoder();
			encoder->setComputePipelineState(pipeline);
			encoder->setTexture(flat, 0);
			encoder->setTexture(cube, 1);
			encoder->setBuffer(out, 0, 0);
			encoder->setBuffer(level, 0, 1);
			encoder->dispatchThreads(Indium::Size { outputs, 1, 1 }, Indium::Size { 1, 1, 1 });
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();

			const float* values = static_cast<const float*>(out->contents());
			bool levels = true;
			for (size_t i = 0; i < 3; ++i) {
				std::printf("level %zu: %g\n", i, values[i * 4]);
				levels = levels && values[i * 4] == 10.0f + i;
			}
			report(levels, "each level of a 2D texture reads back its own value at a run-time lod");
			bool faces = true;
			for (size_t face = 0; face < 6; ++face) {
				std::printf("face %zu level 1: %g\n", face, values[(3 + face) * 4]);
				faces = faces && values[(3 + face) * 4] == 100.0f + 10.0f * face + 1.0f;
			}
			report(faces, "each face of a cube reads back its own value at a run-time lod");
			const float* sizes = values + 9 * 4;
			std::printf("sizes: %g %g %g %g\n", sizes[0], sizes[1], sizes[2], sizes[3]);
			report(sizes[0] == 2 && sizes[1] == 1 && sizes[2] == 1 && sizes[3] == 2, "the size queries see the level asked for");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
