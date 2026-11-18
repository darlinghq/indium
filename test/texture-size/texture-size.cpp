// Copyright (C) 2026 Lubos Dolezel
//
// Kernels from texture-size.metallib that query textures: get_width(), get_height(), get_depth(),
// get_array_size() and get_num_mip_levels() of every texture class that has them (1D, 2D, 3D, cube, array and
// depth textures, and a texture that is only written), with and without a level argument.
// Prints SKIP and exits 0 without a Vulkan device.

#include "texture-size.h"

#include <indium/indium.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
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
		auto library = device->newLibrary(texture_size_metallib, texture_size_metallib_len);
		report(library != nullptr, "the library loads");

		using Indium::PixelFormat;
		using Indium::TextureType;

		// creates a texture and returns what `kernel` stores in its output buffer
		auto query = [&](const char* kernel, TextureType type, PixelFormat format, size_t width, size_t height, size_t depth, size_t layers, size_t levels, Indium::TextureUsage usage, size_t outputs, const std::vector<uint32_t>& expected) {
			try {
				Indium::TextureDescriptor descriptor {};
				descriptor.textureType = type;
				descriptor.pixelFormat = format;
				descriptor.width = width;
				descriptor.height = height;
				descriptor.depth = depth;
				descriptor.arrayLength = layers;
				descriptor.mipmapLevelCount = levels;
				descriptor.usage = usage;
				auto texture = device->newTexture(descriptor);
				auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
				if (!pipeline) {
					throw std::runtime_error("the kernel did not load");
				}
				std::vector<uint32_t> poison(outputs, 0xA5A5A5A5);
				auto output = device->newBuffer(poison.data(), poison.size() * 4, Indium::ResourceOptions::StorageModeShared);
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(pipeline);
				encoder->setTexture(texture, 0);
				encoder->setBuffer(output, 0, 0);
				encoder->dispatchThreads(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				std::vector<uint32_t> got(static_cast<const uint32_t*>(output->contents()), static_cast<const uint32_t*>(output->contents()) + outputs);
				report(got == expected, std::string(kernel) + ": the queried sizes");
			} catch (const std::exception& e) {
				report(false, std::string(kernel) + ": " + e.what());
			}
		};

		const auto read = Indium::TextureUsage::ShaderRead;
		const auto F = PixelFormat::RGBA8Unorm;
		query("size_1d", TextureType::e1D, F, 8, 1, 1, 1, 1, read, 2, { 8, 1 });
		query("size_1d_array", TextureType::e1DArray, F, 8, 1, 1, 3, 1, read, 2, { 8, 3 });
		query("size_2d", TextureType::e2D, F, 8, 4, 1, 1, 3, read, 5, { 8, 4, 4, 1, 3 });
		query("size_2d_array", TextureType::e2DArray, F, 8, 4, 1, 3, 1, read, 3, { 8, 4, 3 });
		query("size_3d", TextureType::e3D, F, 8, 4, 6, 1, 2, read, 4, { 8, 4, 6, 3 });
		query("size_cube", TextureType::eCube, F, 4, 4, 1, 1, 2, read, 3, { 4, 4, 2 });
		query("size_depth", TextureType::e2D, PixelFormat::Depth32Float, 8, 4, 1, 1, 2, read, 3, { 8, 4, 2 });
		query("size_depth_cube", TextureType::eCube, PixelFormat::Depth32Float, 4, 4, 1, 1, 1, read, 2, { 4, 4 });
		query("size_depth_array", TextureType::e2DArray, PixelFormat::Depth32Float, 8, 4, 1, 3, 1, read, 3, { 8, 4, 3 });
		query("size_write_2d", TextureType::e2D, F, 8, 4, 1, 1, 1, Indium::TextureUsage::ShaderWrite, 3, { 8, 4, 1 });

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
