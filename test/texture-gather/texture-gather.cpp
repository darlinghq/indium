// Copyright (C) 2026 Lubos Dolezel
//
// Kernels from texture-gather.metallib that use gather() and gather_compare(): the four texels of the
// bilinear footprint of a color texture (one component of each, x to w) and of a depth texture (the
// depth value, or whether the reference value passes the comparison), with and without an offset, for
// 2D, 2D array and cube textures and float, half, int and uint texels. Each thread gathers at a point
// between four texels of the source; the test compares the four values with the footprint the Metal
// Shading Language specification defines: (i0, j1), (i1, j1), (i1, j0), (i0, j0) with the coordinates
// clamped to the texture.
// Prints SKIP and exits 0 without a Vulkan device.

#include "texture-gather.h"

#include <indium/indium.hpp>

#include <algorithm>
#include <cmath>
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

enum class Element { Float, Half, Int, Uint };

size_t elementSize(Element e) {
	return e == Element::Half ? 2 : 4;
}

// the IEEE half-precision encoding of a small non-negative integer
uint16_t toHalf(unsigned value) {
	if (value == 0) {
		return 0;
	}
	int exponent = 0;
	float mantissa = std::frexp(static_cast<float>(value), &exponent);
	return static_cast<uint16_t>(((exponent + 14) << 10) | static_cast<int>((mantissa * 2 - 1) * 1024));
}

double fromHalf(uint16_t bits) {
	int exponent = (bits >> 10) & 0x1F;
	int mantissa = bits & 0x3FF;
	if (exponent == 0) {
		return std::ldexp(mantissa, -24);
	}
	return std::ldexp(1024 + mantissa, exponent - 25);
}

// the value of channel `channel` of the source texel (x, y) of layer `layer`; small integers, exact in every element type
double texelValue(unsigned layer, int x, int y, unsigned channel) {
	return 1000 * layer + 100 * channel + 10 * y + x;
}

// the depth of the source texel (x, y) of layer `layer`: multiples of 1/48 below 0.7
float depthValue(unsigned layer, int x, int y) {
	return (1 + x + 4 * y + 16 * layer) / 48.0f;
}

int clampTo(int v, int size) {
	return std::max(0, std::min(v, size - 1));
}

struct ColorCase {
	const char* kernel;
	Indium::TextureType type;
	Indium::PixelFormat format;
	Element element;
	size_t layers;
	unsigned component;
	int offsetX, offsetY;
};

struct DepthCase {
	const char* kernel;
	Indium::TextureType type;
	size_t layers;
	bool compare;
	float reference;
	int offsetX, offsetY;
};

constexpr size_t size = 4;

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
		auto library = device->newLibrary(texture_gather_metallib, texture_gather_metallib_len);
		report(library != nullptr, "the library loads");

		using Indium::PixelFormat;
		using Indium::TextureType;

		// Creates a source texture whose every texel (of every layer, or face) comes from `texel`, runs `kernel` over
		// a grid of gridSize x gridSize x layers threads and returns the bytes the kernel stored for each thread.
		auto run = [&](const char* kernel, TextureType type, PixelFormat format, size_t layers, size_t gridSize, size_t outputTexelBytes,
				const std::function<void(unsigned layer, int x, int y, uint8_t* out)>& texel, size_t texelBytes, const float* reference) {
			bool cube = type == TextureType::eCube;
			Indium::TextureDescriptor descriptor {};
			descriptor.textureType = type;
			descriptor.pixelFormat = format;
			descriptor.width = size;
			descriptor.height = size;
			descriptor.depth = 1;
			descriptor.arrayLength = (type == TextureType::e2DArray) ? layers : 1;
			descriptor.usage = Indium::TextureUsage::ShaderRead;
			auto texture = device->newTexture(descriptor);
			for (size_t layer = 0; layer < layers; ++layer) {
				std::vector<uint8_t> bytes(size * size * texelBytes);
				for (size_t y = 0; y < size; ++y) {
					for (size_t x = 0; x < size; ++x) {
						texel(layer, x, y, bytes.data() + (y * size + x) * texelBytes);
					}
				}
				texture->replaceRegion(Indium::Region { Indium::Origin { 0, 0, 0 }, Indium::Size { size, size, 1 } }, 0, layer, bytes.data(), size * texelBytes, 0);
			}

			auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
			if (!pipeline) {
				throw std::runtime_error("the kernel did not load");
			}
			std::vector<uint8_t> poison(size * size * layers * outputTexelBytes, 0xA5);
			auto output = device->newBuffer(poison.data(), poison.size(), Indium::ResourceOptions::StorageModeShared);
			uint32_t dimensions[2] = { static_cast<uint32_t>(size), static_cast<uint32_t>(size) };
			auto sizeBuffer = device->newBuffer(dimensions, sizeof(dimensions), Indium::ResourceOptions::StorageModeShared);
			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->computeCommandEncoder();
			encoder->setComputePipelineState(pipeline);
			encoder->setTexture(texture, 0);
			encoder->setBuffer(output, 0, 0);
			if (reference) {
				encoder->setBuffer(device->newBuffer(reference, sizeof(float), Indium::ResourceOptions::StorageModeShared), 0, 1);
			}
			encoder->setBuffer(sizeBuffer, 0, 2);
			encoder->dispatchThreads(Indium::Size { gridSize, gridSize, cube ? 1 : layers }, Indium::Size { 1, 1, 1 });
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
			return std::vector<uint8_t>(static_cast<const uint8_t*>(output->contents()), static_cast<const uint8_t*>(output->contents()) + poison.size());
		};

		auto decode = [](Element e, const uint8_t* bytes) {
			switch (e) {
				case Element::Float: { float v; std::memcpy(&v, bytes, 4); return static_cast<double>(v); }
				case Element::Half: { uint16_t v; std::memcpy(&v, bytes, 2); return fromHalf(v); }
				case Element::Int: { int32_t v; std::memcpy(&v, bytes, 4); return static_cast<double>(v); }
				case Element::Uint: { uint32_t v; std::memcpy(&v, bytes, 4); return static_cast<double>(v); }
			}
			return 0.0;
		};

		auto encode = [](Element e, double value, uint8_t* bytes) {
			switch (e) {
				case Element::Float: { float v = value; std::memcpy(bytes, &v, 4); } break;
				case Element::Half: { uint16_t v = toHalf(static_cast<unsigned>(value)); std::memcpy(bytes, &v, 2); } break;
				case Element::Int: { int32_t v = value; std::memcpy(bytes, &v, 4); } break;
				case Element::Uint: { uint32_t v = value; std::memcpy(bytes, &v, 4); } break;
			}
		};

		// compares each thread's four values with the footprint around (x + 0.75, y + 0.75), moved by the offset
		auto footprint = [](int x, int y, int offsetX, int offsetY, const std::function<double(int, int)>& at) {
			int i0 = clampTo(x + offsetX, size), i1 = clampTo(x + offsetX + 1, size);
			int j0 = clampTo(y + offsetY, size), j1 = clampTo(y + offsetY + 1, size);
			return std::vector<double> { at(i0, j1), at(i1, j1), at(i1, j0), at(i0, j0) };
		};

		auto colorCase = [&](const ColorCase& c, size_t gridSize = size) {
			std::string name = c.kernel;
			try {
				bool cube = c.type == TextureType::eCube;
				size_t texelBytes = 4 * elementSize(c.element);
				auto bytes = run(c.kernel, c.type, c.format, c.layers, gridSize, texelBytes, [&](unsigned layer, int x, int y, uint8_t* out) {
					for (unsigned ch = 0; ch < 4; ++ch) {
						encode(c.element, texelValue(layer, x, y, ch), out + ch * elementSize(c.element));
					}
				}, texelBytes, nullptr);
				bool ok = true;
				for (size_t layer = 0; layer < (cube ? 1 : c.layers); ++layer) {
					for (size_t y = 0; y < gridSize; ++y) {
						for (size_t x = 0; x < gridSize; ++x) {
							auto want = footprint(x, y, c.offsetX, c.offsetY, [&](int i, int j) { return texelValue(layer, i, j, c.component); });
							const uint8_t* got = bytes.data() + ((layer * size + y) * size + x) * texelBytes;
							for (unsigned k = 0; k < 4; ++k) {
								ok = ok && decode(c.element, got + k * elementSize(c.element)) == want[k];
							}
						}
					}
				}
				report(ok, name + ": the four texels of the footprint");
			} catch (const std::exception& e) {
				report(false, name + ": " + e.what());
			}
		};

		auto depthCase = [&](const DepthCase& c, size_t gridSize = size) {
			std::string name = c.kernel;
			try {
				bool cube = c.type == TextureType::eCube;
				auto bytes = run(c.kernel, c.type, PixelFormat::Depth32Float, c.layers, gridSize, 16, [&](unsigned layer, int x, int y, uint8_t* out) {
					float v = depthValue(layer, x, y);
					std::memcpy(out, &v, 4);
				}, 4, c.compare ? &c.reference : nullptr);
				bool ok = true;
				for (size_t layer = 0; layer < (cube ? 1 : c.layers); ++layer) {
					for (size_t y = 0; y < gridSize; ++y) {
						for (size_t x = 0; x < gridSize; ++x) {
							// a comparison passes (1.0) when the reference value is less than the depth
							auto want = footprint(x, y, c.offsetX, c.offsetY, [&](int i, int j) {
								return c.compare ? (c.reference < depthValue(layer, i, j) ? 1.0 : 0.0) : static_cast<double>(depthValue(layer, i, j));
							});
							const uint8_t* got = bytes.data() + ((layer * size + y) * size + x) * 16;
							for (unsigned k = 0; k < 4; ++k) {
								float v;
								std::memcpy(&v, got + k * 4, 4);
								ok = ok && v == want[k];
							}
						}
					}
				}
				report(ok, name + (c.compare ? ": the comparison against the four texels of the footprint" : ": the four depth values of the footprint"));
			} catch (const std::exception& e) {
				report(false, name + ": " + e.what());
			}
		};

		const auto Float = PixelFormat::RGBA32Float;
		colorCase({ "gather_2d_x", TextureType::e2D, Float, Element::Float, 1, 0, 0, 0 });
		colorCase({ "gather_2d_y", TextureType::e2D, Float, Element::Float, 1, 1, 0, 0 });
		colorCase({ "gather_2d_z", TextureType::e2D, Float, Element::Float, 1, 2, 0, 0 });
		colorCase({ "gather_2d_w", TextureType::e2D, Float, Element::Float, 1, 3, 0, 0 });
		colorCase({ "gather_2d_offset", TextureType::e2D, Float, Element::Float, 1, 1, 1, 0 });
		colorCase({ "gather_2d_uint", TextureType::e2D, PixelFormat::RGBA32Uint, Element::Uint, 1, 2, 0, 0 });
		colorCase({ "gather_2d_int", TextureType::e2D, PixelFormat::RGBA32Sint, Element::Int, 1, 0, 0, 0 });
		colorCase({ "gather_2d_half", TextureType::e2D, PixelFormat::RGBA16Float, Element::Half, 1, 1, 0, 0 });
		colorCase({ "gather_2d_array", TextureType::e2DArray, Float, Element::Float, 3, 1, 0, 0 });
		// the footprint of a cube texture next to the edge of a face reaches into the neighbour, so only the texels inside the face are checked
		colorCase({ "gather_cube", TextureType::eCube, Float, Element::Float, 6, 1, 0, 0 }, size - 1);

		depthCase({ "gather_depth", TextureType::e2D, 1, false, 0, 0, 0 });
		depthCase({ "gather_depth_offset", TextureType::e2D, 1, false, 0, 0, 1 });
		depthCase({ "gather_depth_array", TextureType::e2DArray, 2, false, 0, 0, 0 });
		depthCase({ "gather_compare_depth", TextureType::e2D, 1, true, 0.2f, 0, 0 });
		depthCase({ "gather_compare_depth_offset", TextureType::e2D, 1, true, 0.2f, 1, 0 });
		depthCase({ "gather_compare_depth_array", TextureType::e2DArray, 2, true, 0.5f, 0, 0 });
		depthCase({ "gather_compare_cube", TextureType::eCube, 6, true, 0.2f, 0, 0 }, size - 1);

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
