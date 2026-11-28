// Copyright (C) 2026 Lubos Dolezel
//
// Two AIR constructs in compute kernels: sampling a depth texture (air.sample_depth_*), which returns the
// depth as a scalar, and a constant-length llvm.memcpy between two device buffers.

#include "air-depth-memcpy.h"

#include <indium/indium.hpp>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

static bool check(const char* what, const std::vector<float>& got, const std::vector<float>& want) {
	bool ok = got == want;
	std::cout << (ok ? "PASS " : "FAIL ") << what << ":";
	for (auto value: got) {
		std::cout << " " << value;
	}
	std::cout << std::endl;
	return ok;
}

static void run(Indium::Device* device, const char* function, size_t threads, const std::vector<std::shared_ptr<Indium::Buffer>>& buffers, const std::shared_ptr<Indium::Texture>& texture, const std::shared_ptr<Indium::SamplerState>& sampler) {
	auto library = device->newLibrary(air_depth_memcpy, air_depth_memcpy_len);
	auto pipeline = device->newComputePipelineState(library->newFunction(function));
	auto queue = device->newCommandQueue();
	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	for (size_t i = 0; i < buffers.size(); ++i) {
		encoder->setBuffer(buffers[i], 0, i);
	}
	if (texture) {
		encoder->setTexture(texture, 0);
		encoder->setSamplerState(sampler, 0);
	}
	encoder->dispatchThreads(Indium::Size { threads, 1, 1 }, Indium::Size { threads, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
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

		{
			// a 2x2 depth texture; thread i samples the centre of texel (i & 1, i >> 1)
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::Depth32Float, 2, 2, false);
			auto texture = device->newTexture(descriptor);
			std::vector<float> depth { 0.125f, 0.25f, 0.5f, 1.0f };
			texture->replaceRegion(Indium::Region::make2D(0, 0, 2, 2), 0, depth.data(), 2 * sizeof(float));

			Indium::SamplerDescriptor samplerDescriptor {};
			auto sampler = device->newSamplerState(samplerDescriptor);

			std::vector<float> poison(4, -1.0f);
			auto output = device->newBuffer(poison.data(), poison.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);
			run(device.get(), "sample_depth", 4, { output }, texture, sampler);

			std::vector<float> result(4);
			std::memcpy(result.data(), output->contents(), result.size() * sizeof(float));
			ok = check("depth sample returns the depth of the texel", result, depth) && ok;
		}

		{
			std::vector<float> source { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
			std::vector<float> poison(16, -1.0f);
			auto src = device->newBuffer(source.data(), source.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);
			auto dst = device->newBuffer(poison.data(), poison.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);
			run(device.get(), "copy16", 4, { src, dst }, nullptr, nullptr);

			std::vector<float> result(16);
			std::memcpy(result.data(), dst->contents(), result.size() * sizeof(float));
			ok = check("llvm.memcpy copies 16 bytes per thread", result, source) && ok;
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return ok ? 0 : 1;
};
