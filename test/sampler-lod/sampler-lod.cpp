// Copyright (C) 2026 Darling Developers
//
// Samples a two-level texture (level 0 white, level 1 black) at explicit LODs 0, 0.5, 1, ... 3.5.
// Metal reads only the base level through a sampler with no mip filter, whatever LOD the shader
// asks for; with a nearest mip filter the LOD selects the level.

#include "sampler-lod.h"

#include <indium/indium.hpp>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

static constexpr size_t threadCount = 8;

static std::vector<float> sampleLevels(Indium::Device* device, Indium::SamplerMipFilter mipFilter) {
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 2, 2, true);
	descriptor.mipmapLevelCount = 2;
	auto texture = device->newTexture(descriptor);
	std::vector<uint8_t> white(2 * 2 * 4, 0xff), black(4, 0);
	texture->replaceRegion(Indium::Region::make2D(0, 0, 2, 2), 0, white.data(), 2 * 4);
	texture->replaceRegion(Indium::Region::make2D(0, 0, 1, 1), 1, black.data(), 4);

	Indium::SamplerDescriptor samplerDescriptor {};
	samplerDescriptor.mipFilter = mipFilter;
	auto sampler = device->newSamplerState(samplerDescriptor);

	std::vector<float> poison(threadCount, -1.0f);
	auto output = device->newBuffer(poison.data(), poison.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);
	auto library = device->newLibrary(sampler_lod, sampler_lod_len);
	auto pipeline = device->newComputePipelineState(library->newFunction("sample_lod"));
	auto queue = device->newCommandQueue();

	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(output, 0, 0);
	encoder->setTexture(texture, 0);
	encoder->setSamplerState(sampler, 0);
	encoder->dispatchThreads(Indium::Size { threadCount, 1, 1 }, Indium::Size { threadCount, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();

	std::vector<float> result(threadCount);
	std::memcpy(result.data(), output->contents(), result.size() * sizeof(float));
	return result;
}

static bool check(const char* what, const std::vector<float>& got, const std::vector<float>& want) {
	bool ok = got == want;
	std::cout << (ok ? "PASS " : "FAIL ") << what << ":";
	for (auto value: got) {
		std::cout << " " << value;
	}
	std::cout << std::endl;
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

		ok = check("no mip filter reads level 0", sampleLevels(device.get(), Indium::SamplerMipFilter::NotMipmapped), { 1, 1, 1, 1, 1, 1, 1, 1 }) && ok;
		// nearest picks level ceil(lod + 0.5) - 1, clamped to the last level
		ok = check("nearest mip filter follows the LOD", sampleLevels(device.get(), Indium::SamplerMipFilter::Nearest), { 1, 1, 0, 0, 0, 0, 0, 0 }) && ok;

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return ok ? 0 : 1;
};
