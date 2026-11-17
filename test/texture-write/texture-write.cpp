// Copyright (C) 2026 Lubos Dolezel
//
// Kernels from texture-write.metallib that write textures: write() on access::write and access::read_write
// textures of every dimensionality, array, and component type the Metal Shading Language defines for them.
// Each kernel writes a value computed from the thread's position; the test reads the texture back and
// compares every texel with what the Metal Shading Language specification defines for that write.
// Prints SKIP and exits 0 without a Vulkan device.

#include "texture-write.h"

#include <indium/indium.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	failures += !ok;
}

using TexelFn = std::function<void(size_t x, size_t y, size_t z, uint8_t* texel)>;

struct Case {
	const char* kernel;
	Indium::TextureType type;
	Indium::PixelFormat format;
	size_t width, height, depth, layers;
	size_t texelSize;
	// the bytes of the texel at (x, y, z or layer), as the kernel stores it
	TexelFn expected;
	// what the texture holds before the kernel runs (0xA5 bytes when empty)
	TexelFn initial;
};

template<typename T, size_t N>
void put(uint8_t* texel, const T (&values)[N]) {
	std::memcpy(texel, values, sizeof(values));
}

// the IEEE half-precision encoding of a small positive value (the kernels write integers and 0.5)
uint16_t half(float value) {
	if (value == 0) {
		return 0;
	}
	int exponent = 0;
	float mantissa = std::frexp(value, &exponent);
	return static_cast<uint16_t>(((exponent + 14) << 10) | static_cast<int>((mantissa * 2 - 1) * 1024));
}

TexelFn f32(std::function<void(size_t, size_t, size_t, float*)> fill) {
	return [fill](size_t x, size_t y, size_t z, uint8_t* texel) {
		float v[4];
		fill(x, y, z, v);
		std::memcpy(texel, v, sizeof(v));
	};
}

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

		auto library = device->newLibrary(texture_write_metallib, texture_write_metallib_len);
		report(library != nullptr, "the library loads");

		using Indium::PixelFormat;
		using Indium::TextureType;

		std::vector<Case> cases {
			{ "write_2d", TextureType::e2D, PixelFormat::RGBA32Float, 8, 4, 1, 1, 16,
				f32([](size_t x, size_t y, size_t, float* v) { v[0] = x; v[1] = y; v[2] = x + y; v[3] = 1; }), nullptr },
			{ "write_2d_uint", TextureType::e2D, PixelFormat::RGBA32Uint, 8, 4, 1, 1, 16,
				[](size_t x, size_t y, size_t, uint8_t* t) { uint32_t v[4] = { uint32_t(x), uint32_t(y), uint32_t(x * 10 + y), 7 }; put(t, v); }, nullptr },
			{ "write_2d_int", TextureType::e2D, PixelFormat::RGBA32Sint, 8, 4, 1, 1, 16,
				[](size_t x, size_t y, size_t, uint8_t* t) { int32_t v[4] = { -int32_t(x), int32_t(y), -3, 5 }; put(t, v); }, nullptr },
			{ "write_2d_half", TextureType::e2D, PixelFormat::RGBA16Float, 8, 4, 1, 1, 8,
				[](size_t x, size_t y, size_t, uint8_t* t) { uint16_t v[4] = { half(x), half(y), half(0.5f), half(1) }; put(t, v); }, nullptr },
			{ "write_1d", TextureType::e1D, PixelFormat::RGBA32Float, 8, 1, 1, 1, 16,
				f32([](size_t x, size_t, size_t, float* v) { v[0] = x; v[1] = 2 * x; v[2] = 0; v[3] = 1; }), nullptr },
			{ "write_3d", TextureType::e3D, PixelFormat::RGBA32Float, 4, 4, 3, 1, 16,
				f32([](size_t x, size_t y, size_t z, float* v) { v[0] = x; v[1] = y; v[2] = z; v[3] = 1; }), nullptr },
			{ "write_2d_array", TextureType::e2DArray, PixelFormat::RGBA32Float, 4, 4, 1, 3, 16,
				f32([](size_t x, size_t y, size_t z, float* v) { v[0] = x; v[1] = y; v[2] = z; v[3] = 1; }), nullptr },
			{ "write_1d_array", TextureType::e1DArray, PixelFormat::RGBA32Float, 8, 1, 1, 3, 16,
				f32([](size_t x, size_t, size_t z, float* v) { v[0] = x; v[1] = z; v[2] = 0; v[3] = 1; }), nullptr },
			{ "read_write_2d", TextureType::e2D, PixelFormat::RGBA32Float, 8, 4, 1, 1, 16,
				f32([](size_t x, size_t y, size_t, float* v) { v[0] = (x + 1) * 2 + 1; v[1] = y * 2; v[2] = (x * y) * 2; v[3] = 2; }),
				f32([](size_t x, size_t y, size_t, float* v) { v[0] = x + 1; v[1] = y; v[2] = x * y; v[3] = 1; }) },
			{ "read_write_1d", TextureType::e1D, PixelFormat::RGBA32Float, 8, 1, 1, 1, 16,
				f32([](size_t x, size_t, size_t, float* v) { v[0] = x + 1; v[1] = 2 * x + 1; v[2] = 1; v[3] = 2; }),
				f32([](size_t x, size_t, size_t, float* v) { v[0] = x; v[1] = 2 * x; v[2] = 0; v[3] = 1; }) },
			{ "read_write_2d_array", TextureType::e2DArray, PixelFormat::RGBA32Float, 4, 4, 1, 3, 16,
				f32([](size_t x, size_t y, size_t z, float* v) { v[0] = x + 1; v[1] = y + 1; v[2] = z + 1; v[3] = 2; }),
				f32([](size_t x, size_t y, size_t z, float* v) { v[0] = x; v[1] = y; v[2] = z; v[3] = 1; }) },
		};

		auto makeTexture = [&](const Case& c) {
			Indium::TextureDescriptor descriptor {};
			descriptor.textureType = c.type;
			descriptor.pixelFormat = c.format;
			descriptor.width = c.width;
			descriptor.height = c.height;
			descriptor.depth = c.depth;
			descriptor.arrayLength = c.layers;
			descriptor.usage = Indium::TextureUsage::ShaderRead | Indium::TextureUsage::ShaderWrite;
			return device->newTexture(descriptor);
		};

		auto wholeSlice = [](const Case& c) {
			return Indium::Region { Indium::Origin { 0, 0, 0 }, Indium::Size { c.width, c.height, c.depth } };
		};

		// the texels of every layer in order, then every depth slice, row after row
		auto readBack = [&](const Case& c, const std::shared_ptr<Indium::Texture>& texture) {
			std::vector<uint8_t> bytes(c.width * c.height * c.depth * c.layers * c.texelSize, 0xEE);
			size_t sliceSize = c.width * c.height * c.depth * c.texelSize;
			for (size_t layer = 0; layer < c.layers; ++layer) {
				texture->getBytes(wholeSlice(c), 0, layer, bytes.data() + layer * sliceSize, c.width * c.texelSize, c.width * c.height * c.texelSize);
			}
			return bytes;
		};

		auto upload = [&](const Case& c, const std::shared_ptr<Indium::Texture>& texture, const std::vector<uint8_t>& bytes, size_t level = 0) {
			size_t sliceSize = c.width * c.height * c.depth * c.texelSize;
			for (size_t layer = 0; layer < c.layers; ++layer) {
				texture->replaceRegion(wholeSlice(c), level, layer, bytes.data() + layer * sliceSize, c.width * c.texelSize, c.width * c.height * c.texelSize);
			}
		};

		auto bytesOf = [&](const Case& c, const TexelFn& fn) {
			std::vector<uint8_t> bytes(c.width * c.height * c.depth * c.layers * c.texelSize, 0);
			for (size_t layer = 0; layer < c.layers; ++layer) {
				for (size_t z = 0; z < c.depth; ++z) {
					for (size_t y = 0; y < c.height; ++y) {
						for (size_t x = 0; x < c.width; ++x) {
							size_t index = (((layer * c.depth + z) * c.height + y) * c.width + x) * c.texelSize;
							fn(x, y, c.depth > 1 ? z : layer, bytes.data() + index);
						}
					}
				}
			}
			return bytes;
		};

		auto run = [&](const Case& c, const std::vector<std::shared_ptr<Indium::Texture>>& textures) {
			auto pipeline = device->newComputePipelineState(library->newFunction(c.kernel));
			if (!pipeline) {
				return false;
			}
			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->computeCommandEncoder();
			encoder->setComputePipelineState(pipeline);
			for (size_t i = 0; i < textures.size(); ++i) {
				encoder->setTexture(textures[i], i);
			}
			// arrays take the layer as the last grid coordinate
			Indium::Size grid { c.width, c.height, c.depth };
			if (c.layers > 1) {
				(c.type == TextureType::e1DArray ? grid.height : grid.depth) = c.layers;
			}
			encoder->dispatchThreads(grid, Indium::Size { 1, 1, 1 });
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
			return true;
		};

		for (auto& c: cases) {
			std::string name = c.kernel;
			try {
				auto texture = makeTexture(c);
				upload(c, texture, c.initial ? bytesOf(c, c.initial) : std::vector<uint8_t>(c.width * c.height * c.depth * c.layers * c.texelSize, 0xA5));
				report(run(c, { texture }), name + ": the kernel loads and runs");
				report(readBack(c, texture) == bytesOf(c, c.expected), name + ": every texel holds the value written");
			} catch (const std::exception& e) {
				report(false, name + ": " + e.what());
			}
		}

		// Textures read with access::read (a sampled image) and written with access::write (a storage image) by
		// the same kernel: every texel of the output is `transform` of the input texel at the same position
		// (read from `inputLevel` of the input).
		auto copyCase = [&](const char* kernel, Case c, const TexelFn& input, const TexelFn& output, size_t inputLevel = 0) {
			std::string name = kernel;
			try {
				c.kernel = kernel;
				Case inputCase = c;
				// level N is 2^N times smaller than level 0 in width and height
				inputCase.width <<= inputLevel;
				inputCase.height <<= inputLevel;
				Indium::TextureDescriptor descriptor {};
				descriptor.textureType = c.type;
				descriptor.pixelFormat = c.format;
				descriptor.width = inputCase.width;
				descriptor.height = inputCase.height;
				descriptor.depth = c.depth;
				descriptor.arrayLength = c.layers;
				descriptor.mipmapLevelCount = inputLevel + 1;
				descriptor.usage = Indium::TextureUsage::ShaderRead;
				auto source = device->newTexture(descriptor);
				auto target = makeTexture(c);
				auto in = bytesOf(c, input);
				upload(c, source, in, inputLevel);
				upload(c, target, std::vector<uint8_t>(in.size(), 0xA5));
				report(run(c, { source, target }), name + ": the kernel loads and runs");
				report(readBack(c, target) == bytesOf(c, output), name + ": every texel written is the one read, as the kernel transforms it");
			} catch (const std::exception& e) {
				report(false, name + ": " + e.what());
			}
		};

		auto pattern = f32([](size_t x, size_t y, size_t z, float* v) { v[0] = x; v[1] = y; v[2] = z; v[3] = x * 4 + y + z * 100 + 0.5f; });
		auto same = pattern;
		copyCase("copy_read_to_write", { nullptr, TextureType::e2D, PixelFormat::RGBA32Float, 8, 4, 1, 1, 16, nullptr, nullptr },
			f32([](size_t x, size_t y, size_t, float* v) { v[0] = x; v[1] = y; v[2] = x * y; v[3] = 9; }),
			f32([](size_t x, size_t y, size_t, float* v) { v[0] = 9; v[1] = x * y; v[2] = y; v[3] = x; }));
		copyCase("copy_1d", { nullptr, TextureType::e1D, PixelFormat::RGBA32Float, 8, 1, 1, 1, 16, nullptr, nullptr }, pattern, same);
		copyCase("copy_3d", { nullptr, TextureType::e3D, PixelFormat::RGBA32Float, 4, 4, 3, 1, 16, nullptr, nullptr }, pattern, same);
		copyCase("copy_2d_array", { nullptr, TextureType::e2DArray, PixelFormat::RGBA32Float, 4, 4, 1, 3, 16, nullptr, nullptr }, pattern, same);
		copyCase("copy_1d_array", { nullptr, TextureType::e1DArray, PixelFormat::RGBA32Float, 8, 1, 1, 3, 16, nullptr, nullptr }, pattern, same);
		copyCase("copy_2d_uint", { nullptr, TextureType::e2D, PixelFormat::RGBA32Uint, 8, 4, 1, 1, 16, nullptr, nullptr },
			[](size_t x, size_t y, size_t, uint8_t* t) { uint32_t v[4] = { uint32_t(x), uint32_t(y), 4000000000u, 7 }; put(t, v); },
			[](size_t x, size_t y, size_t, uint8_t* t) { uint32_t v[4] = { uint32_t(x + 1), uint32_t(y + 1), 4000000001u, 8 }; put(t, v); });
		copyCase("copy_2d_half", { nullptr, TextureType::e2D, PixelFormat::RGBA16Float, 8, 4, 1, 1, 8, nullptr, nullptr },
			[](size_t x, size_t y, size_t, uint8_t* t) { uint16_t v[4] = { half(x), half(y), half(0.5f), half(1) }; put(t, v); },
			[](size_t x, size_t y, size_t, uint8_t* t) { uint16_t v[4] = { half(x), half(y), half(0.5f), half(1) }; put(t, v); });
		// the output is half the width and height of the input's level 0, as the level it reads is
		copyCase("copy_2d_level", { nullptr, TextureType::e2D, PixelFormat::RGBA32Float, 4, 2, 1, 1, 16, nullptr, nullptr }, pattern, same, 1);

		// get_width() and get_height() of a writable texture
		try {
			Case c { "size_2d", TextureType::e2D, PixelFormat::RGBA32Float, 8, 4, 1, 1, 16, nullptr, nullptr };
			auto texture = makeTexture(c);
			uint32_t poison[2] = { 0xA5A5A5A5, 0xA5A5A5A5 };
			auto output = device->newBuffer(poison, sizeof(poison), Indium::ResourceOptions::StorageModeShared);
			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->computeCommandEncoder();
			encoder->setComputePipelineState(device->newComputePipelineState(library->newFunction("size_2d")));
			encoder->setTexture(texture, 0);
			encoder->setBuffer(output, 0, 0);
			encoder->dispatchThreads(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
			auto sizes = static_cast<const uint32_t*>(output->contents());
			report(sizes[0] == 8 && sizes[1] == 4, "size_2d: get_width() and get_height() of a writable texture");
		} catch (const std::exception& e) {
			report(false, std::string("size_2d: ") + e.what());
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
