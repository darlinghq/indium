// Copyright (C) 2026 Lubos Dolezel
//
// A texture made from the rows of a buffer (Buffer::newTexture()):
//  - what is written to the buffer is what a shader and a readback of the texture see, and what is written to
//    the texture is in the buffer, at the offset and with the row pitch the texture was made with;
//  - the texture reports its buffer, offset and bytes per row;
//  - a texture that does not fit the buffer, has a row pitch the device does not use, or is not a plain 2D
//    texture of an uncompressed format is refused.
// The shader is mslc's output for read-float.metal in this directory.
// Prints SKIP and exits 0 without a Vulkan device or without linear textures of the format.

#include "read-float-spv.h"

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
constexpr size_t width = 5;
constexpr size_t height = 4;

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
		size_t alignment = device->minimumLinearTextureAlignment(PixelFormat::RGBA8Unorm);
		std::printf("linear row alignment: %zu\n", alignment);
		report(alignment >= 4 && alignment % 4 == 0, "the row alignment of RGBA8Unorm is a whole number of texels");
		report(device->minimumLinearTextureAlignment(PixelFormat::BC1_RGBA) == 0, "a compressed format has no linear alignment");
		report(device->minimumLinearTextureAlignment(PixelFormat::Depth32Float) == 0, "a depth format has no linear alignment");

		size_t bytesPerRow = (width * 4 + alignment - 1) / alignment * alignment;
		size_t offset = alignment * 2;
		auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(PixelFormat::RGBA8Unorm, width, height, false);
		descriptor.usage = Indium::TextureUsage::ShaderRead;
		descriptor.resourceOptions = Indium::ResourceOptions::StorageModeShared;
		descriptor.allowGPUOptimizedContents = false;

		auto buffer = device->newBuffer(offset + bytesPerRow * height, Indium::ResourceOptions::StorageModeShared);
		auto bytes = static_cast<uint8_t*>(buffer->contents());
		std::memset(bytes, 0, buffer->length());
		for (size_t y = 0; y < height; ++y) {
			for (size_t x = 0; x < width; ++x) {
				uint8_t* texel = bytes + offset + y * bytesPerRow + x * 4;
				texel[0] = static_cast<uint8_t>(10 + x * 20);
				texel[1] = static_cast<uint8_t>(10 + y * 30);
				texel[2] = static_cast<uint8_t>(200);
				texel[3] = static_cast<uint8_t>(255 - x - y);
			}
		}

		auto texture = buffer->newTexture(descriptor, offset, bytesPerRow);
		if (!texture) {
			std::printf("SKIP the device has no linear sampled textures of this format\n");
		} else {
			report(texture->buffer() == buffer, "the texture reports its buffer");
			report(texture->bufferOffset() == offset && texture->bufferBytesPerRow() == bytesPerRow, "the texture reports its offset and bytes per row");
			report(texture->width() == width && texture->height() == height && texture->pixelFormat() == PixelFormat::RGBA8Unorm, "the texture has the size and format asked for");

			// the buffer's bytes are the texture's pixels
			std::vector<uint8_t> back(width * height * 4);
			texture->getBytes(Indium::Region::make2D(0, 0, width, height), 0, back.data(), width * 4);
			bool same = true;
			for (size_t y = 0; y < height; ++y) {
				same = same && std::memcmp(back.data() + y * width * 4, bytes + offset + y * bytesPerRow, width * 4) == 0;
			}
			report(same, "a readback of the texture is what is in the buffer");

			// a shader reads them too
			Indium::LibraryReflection reflection;
			auto& function = reflection.functions["readFloat"];
			function.functionType = Indium::FunctionType::Kernel;
			function.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
			function.bindings.push_back(Indium::BindingDescriptor { 0, 1, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Sample });
			std::string error;
			auto library = device->newLibrary(read_float_spv, read_float_spv_len, reflection, &error);
			report(library != nullptr, "the read kernel loads");
			if (library) {
				auto pipeline = device->newComputePipelineState(library->newFunction("readFloat"));
				auto read = [&]() {
					auto result = device->newBuffer(16, Indium::ResourceOptions::StorageModeShared);
					auto commandBuffer = queue->commandBuffer();
					auto encoder = commandBuffer->computeCommandEncoder();
					encoder->setComputePipelineState(pipeline);
					encoder->setBuffer(result, 0, 0);
					encoder->setTexture(texture, 0);
					encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
					encoder->endEncoding();
					commandBuffer->commit();
					commandBuffer->waitUntilCompleted();
					std::vector<float> texel(4);
					std::memcpy(texel.data(), result->contents(), 16);
					return texel;
				};
				auto texel = read();
				report(std::fabs(texel[0] - 10 / 255.0f) < 0.002f && std::fabs(texel[1] - 10 / 255.0f) < 0.002f && std::fabs(texel[2] - 200 / 255.0f) < 0.002f,
					"a shader reads the pixels the CPU wrote to the buffer");

				// a later CPU write to the buffer is seen by the texture
				bytes[offset] = 77;
				texel = read();
				report(std::fabs(texel[0] - 77 / 255.0f) < 0.002f, "a later write to the buffer is seen by the texture");
			}

			// the GPU copy into the texture lands in the buffer
			const uint8_t pixel[4] = { 1, 2, 3, 4 };
			texture->replaceRegion(Indium::Region::make2D(2, 1, 1, 1), 0, pixel, 4);
			report(std::memcmp(bytes + offset + 1 * bytesPerRow + 2 * 4, pixel, 4) == 0, "replaceRegion on the texture writes the buffer");

			// refusals
			report(buffer->newTexture(descriptor, offset, bytesPerRow + alignment) == nullptr, "a row pitch the device does not use is refused");
			report(buffer->newTexture(descriptor, offset, bytesPerRow - 4) == nullptr, "a row pitch too small for the width is refused");
			report(buffer->newTexture(descriptor, offset + alignment * 4, bytesPerRow) == nullptr, "a texture that runs past the end of the buffer is refused");
			report(buffer->newTexture(descriptor, offset, 0) == nullptr, "a zero row pitch is refused");

			auto mipmapped = descriptor;
			mipmapped.mipmapLevelCount = 2;
			report(buffer->newTexture(mipmapped, offset, bytesPerRow) == nullptr, "a texture with mip levels is refused");
			auto array = descriptor;
			array.textureType = Indium::TextureType::e2DArray;
			array.arrayLength = 2;
			report(buffer->newTexture(array, offset, bytesPerRow) == nullptr, "a texture array is refused");
			auto compressed = descriptor;
			compressed.pixelFormat = PixelFormat::BC1_RGBA;
			report(buffer->newTexture(compressed, offset, bytesPerRow) == nullptr, "a compressed format is refused");
			auto depth = descriptor;
			depth.pixelFormat = PixelFormat::Depth32Float;
			report(buffer->newTexture(depth, offset, bytesPerRow) == nullptr, "a depth format is refused");
			auto managed = descriptor;
			managed.resourceOptions = Indium::ResourceOptions::StorageModeManaged;
			report(buffer->newTexture(managed, offset, bytesPerRow) == nullptr, "a storage mode other than the buffer's is refused");

			// the texture keeps the buffer alive
			std::weak_ptr<Indium::Buffer> weak = buffer;
			buffer.reset();
			report(!weak.expired() && texture->buffer() != nullptr, "the texture keeps its buffer alive");
			texture.reset();
			report(weak.expired(), "the buffer goes away with the texture");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
