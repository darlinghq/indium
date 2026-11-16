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
#include "write-texel-spv.h"

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
		report(alignment == 16, "the row alignment of RGBA8Unorm is 16 bytes");
		report(device->minimumLinearTextureAlignment(PixelFormat::BC1_RGBA) == 0, "a compressed format has no linear alignment");
		report(device->minimumLinearTextureAlignment(PixelFormat::Depth32Float) == 0, "a depth format has no linear alignment");

		// The row pitch is the application's: any multiple of 16 bytes that holds a row works, whatever pitch the
		// device gives a linear image of its own (the pitches here are such a pitch on some devices and not on others).
		const size_t rowBytes = width * 4;
		const size_t minPitch = (rowBytes + alignment - 1) / alignment * alignment; // the smallest pitch that holds a row
		auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(PixelFormat::RGBA8Unorm, width, height, false);
		descriptor.usage = Indium::TextureUsage::ShaderRead | Indium::TextureUsage::ShaderWrite;
		descriptor.resourceOptions = Indium::ResourceOptions::StorageModeShared;
		descriptor.allowGPUOptimizedContents = false;

		Indium::LibraryReflection reflection;
		auto& function = reflection.functions["readFloat"];
		function.functionType = Indium::FunctionType::Kernel;
		function.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
		function.bindings.push_back(Indium::BindingDescriptor { 0, 1, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Sample });
		std::string error;
		auto library = device->newLibrary(read_float_spv, read_float_spv_len, reflection, &error);
		report(library != nullptr, "the read kernel loads");
		auto pipeline = library ? device->newComputePipelineState(library->newFunction("readFloat")) : nullptr;

		Indium::LibraryReflection writeReflection;
		auto& writeFunction = writeReflection.functions["writeTexel"];
		writeFunction.functionType = Indium::FunctionType::Kernel;
		writeFunction.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Write });
		auto writeLibrary = device->newLibrary(write_texel_spv, write_texel_spv_len, writeReflection, &error);
		report(writeLibrary != nullptr, "the write kernel loads");
		auto writePipeline = writeLibrary ? device->newComputePipelineState(writeLibrary->newFunction("writeTexel")) : nullptr;

		auto fill = [&](uint8_t* bytes, size_t offset, size_t bytesPerRow, size_t length) {
			std::memset(bytes, 0, length);
			for (size_t y = 0; y < height; ++y) {
				for (size_t x = 0; x < width; ++x) {
					uint8_t* texel = bytes + offset + y * bytesPerRow + x * 4;
					texel[0] = static_cast<uint8_t>(10 + x * 20);
					texel[1] = static_cast<uint8_t>(10 + y * 30);
					texel[2] = static_cast<uint8_t>(200);
					texel[3] = static_cast<uint8_t>(255 - x - y);
				}
			}
		};

		auto checkPitch = [&](size_t bytesPerRow, size_t offset) {
			char label[96];
			std::snprintf(label, sizeof label, "%zu bytes per row at offset %zu", bytesPerRow, offset);
			auto buffer = device->newBuffer(offset + bytesPerRow * height, Indium::ResourceOptions::StorageModeShared);
			auto* bytes = static_cast<uint8_t*>(buffer->contents());
			fill(bytes, offset, bytesPerRow, buffer->length());
			auto texture = buffer->newTexture(descriptor, offset, bytesPerRow);
			auto what = [&](const char* text) {
				static char line[192];
				std::snprintf(line, sizeof line, "%s (%s)", text, label);
				return static_cast<const char*>(line);
			};
			report(texture != nullptr, what("a texture can be made"));
			if (!texture) {
				return;
			}
			report(texture->buffer() == buffer && texture->bufferOffset() == offset && texture->bufferBytesPerRow() == bytesPerRow, what("the texture reports its buffer, offset and bytes per row"));
			report(texture->width() == width && texture->height() == height && texture->pixelFormat() == PixelFormat::RGBA8Unorm, what("the texture has the size and format asked for"));

			// the buffer's bytes are the texture's pixels
			std::vector<uint8_t> back(rowBytes * height);
			texture->getBytes(Indium::Region::make2D(0, 0, width, height), 0, back.data(), rowBytes);
			bool same = true;
			for (size_t y = 0; y < height; ++y) {
				same = same && std::memcmp(back.data() + y * rowBytes, bytes + offset + y * bytesPerRow, rowBytes) == 0;
			}
			report(same, what("a readback of the texture is what is in the buffer"));

			if (pipeline) {
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
					what("a shader reads the pixels the CPU wrote to the buffer"));

				// a later CPU write to the buffer is seen by the texture
				bytes[offset] = 77;
				texel = read();
				report(std::fabs(texel[0] - 77 / 255.0f) < 0.002f, what("a later write to the buffer is seen by the texture"));
			}

			// the GPU copy into the texture lands in the buffer
			const uint8_t pixel[4] = { 1, 2, 3, 4 };
			texture->replaceRegion(Indium::Region::make2D(2, 1, 1, 1), 0, pixel, 4);
			report(std::memcmp(bytes + offset + 1 * bytesPerRow + 2 * 4, pixel, 4) == 0, what("replaceRegion on the texture writes the buffer"));
			// and a row of the buffer between the texels of a row is left alone
			std::vector<uint8_t> row(bytesPerRow, 0x5A);
			bool untouched = true;
			if (bytesPerRow > rowBytes) {
				fill(bytes, offset, bytesPerRow, buffer->length());
				std::memset(bytes + offset + rowBytes, 0x5A, bytesPerRow - rowBytes);
				texture->replaceRegion(Indium::Region::make2D(0, 0, width, 1), 0, back.data(), rowBytes);
				untouched = std::memcmp(bytes + offset + rowBytes, row.data(), bytesPerRow - rowBytes) == 0;
			}
			report(untouched, what("the padding at the end of a row is left alone"));

			if (!writePipeline || !pipeline) {
				return;
			}
			auto dispatchWrite = [&](Indium::ComputeCommandEncoder& encoder) {
				encoder.setComputePipelineState(writePipeline);
				encoder.setTexture(texture, 0);
				encoder.dispatchThreads(Indium::Size { width, height, 1 }, Indium::Size { 1, 1, 1 });
			};
			auto expectWritten = [&](const uint8_t* rows, size_t pitch) {
				bool all = true;
				for (size_t y = 0; y < height; ++y) {
					for (size_t x = 0; x < width; ++x) {
						const uint8_t* texel = rows + y * pitch + x * 4;
						all = all && std::abs(texel[0] - static_cast<int>(x * 25.5f + 0.5f)) <= 1 && std::abs(texel[1] - static_cast<int>(y * 25.5f + 0.5f)) <= 1
							&& std::abs(texel[2] - 128) <= 1 && texel[3] == 255;
					}
				}
				return all;
			};

			// a kernel's writes to the texture are in the buffer once the command buffer completed
			fill(bytes, offset, bytesPerRow, buffer->length());
			{
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				dispatchWrite(*encoder);
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
			}
			report(expectWritten(bytes + offset, bytesPerRow), what("a kernel's writes to the texture are in the buffer"));

			// what one encoder writes to the texture, a later encoder of the same command buffer sees in the buffer
			{
				auto copy = device->newBuffer(bytesPerRow * height, Indium::ResourceOptions::StorageModeShared);
				std::memset(copy->contents(), 0, copy->length());
				std::memset(bytes, 0, buffer->length());
				auto commandBuffer = queue->commandBuffer();
				auto compute = commandBuffer->computeCommandEncoder();
				dispatchWrite(*compute);
				compute->endEncoding();
				auto blit = commandBuffer->blitCommandEncoder();
				blit->copy(buffer, offset, copy, 0, bytesPerRow * height);
				blit->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				report(expectWritten(static_cast<const uint8_t*>(copy->contents()), bytesPerRow), what("a later encoder reads the buffer rows an earlier one wrote through the texture"));
			}

			// what an encoder writes to the buffer, a later encoder of the same command buffer reads through the texture
			{
				auto source = device->newBuffer(bytesPerRow * height, Indium::ResourceOptions::StorageModeShared);
				fill(static_cast<uint8_t*>(source->contents()), 0, bytesPerRow, source->length());
				static_cast<uint8_t*>(source->contents())[0] = 99;
				std::memset(bytes, 0, buffer->length());
				auto result = device->newBuffer(16, Indium::ResourceOptions::StorageModeShared);
				auto commandBuffer = queue->commandBuffer();
				auto blit = commandBuffer->blitCommandEncoder();
				blit->copy(source, 0, buffer, offset, bytesPerRow * height);
				blit->endEncoding();
				auto compute = commandBuffer->computeCommandEncoder();
				compute->setComputePipelineState(pipeline);
				compute->setBuffer(result, 0, 0);
				compute->setTexture(texture, 0);
				compute->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
				compute->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				float texel[4];
				std::memcpy(texel, result->contents(), 16);
				report(std::fabs(texel[0] - 99 / 255.0f) < 0.002f && std::fabs(texel[2] - 200 / 255.0f) < 0.002f, what("a later encoder reads through the texture what an earlier one wrote to the buffer"));
			}
		};

		const size_t offset = alignment * 2;
		checkPitch(minPitch, offset);
		for (size_t pitch : { size_t(32), size_t(48), size_t(64), size_t(96), size_t(128), size_t(144), size_t(192), size_t(256), size_t(272) }) {
			if (pitch >= rowBytes && pitch % 16 == 0) {
				checkPitch(pitch, offset);
			}
		}
		checkPitch(32, 16);

		{
			// refusals
			size_t bytesPerRow = minPitch;
			auto buffer = device->newBuffer(offset + bytesPerRow * height, Indium::ResourceOptions::StorageModeShared);
			auto texture = buffer->newTexture(descriptor, offset, bytesPerRow);
			report(texture != nullptr, "a texture can be made for the refusals");
			report(buffer->newTexture(descriptor, offset, 16) == nullptr, "a row pitch too small for the width is refused");
			report(buffer->newTexture(descriptor, offset, minPitch + 4) == nullptr, "a row pitch that is not a multiple of 16 bytes is refused");
			report(buffer->newTexture(descriptor, offset + alignment * 4, bytesPerRow) == nullptr, "a texture that runs past the end of the buffer is refused");
			report(buffer->newTexture(descriptor, offset + 4, bytesPerRow) == nullptr, "an offset that is not a multiple of 16 bytes is refused");
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
