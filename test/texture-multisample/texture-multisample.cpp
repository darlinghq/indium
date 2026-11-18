// Copyright (C) 2026 Lubos Dolezel
//
// Kernels from texture-multisample.metallib that read multisample textures: read(coordinates, sample) of float,
// half and uint color textures, of a depth texture and of an array texture, and the size and sample count
// queries. The textures are rendered by the test: the color ones are cleared to blue and a triangle paints
// everything left of the middle of the second pixel of each row red, so that the pixel left of the edge has
// every sample red, the pixel the edge crosses has the samples whose standard 4x position lies left of the
// middle red and the others blue, and the pixels right of it are blue.
// Prints SKIP and exits 0 without a Vulkan device.

#include "texture-multisample.h"
#include "half-plane.vert.h"
#include "paint.frag.h"
#include "paint-uint.frag.h"

#include <indium/indium.hpp>

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

constexpr size_t size = 4;
constexpr size_t samples = 4;

// whether sample `s` of the pixel at column `x` is painted: the edge is in the middle of column 1, and the
// standard locations of 4 samples have an x below one half for samples 0 and 2
bool painted(size_t x, size_t s) {
	return x == 0 || (x == 1 && (s == 0 || s == 2));
}

uint16_t toHalf(unsigned value) {
	if (value == 0) {
		return 0;
	}
	int exponent = 0;
	float mantissa = std::frexp(static_cast<float>(value), &exponent);
	return static_cast<uint16_t>(((exponent + 14) << 10) | static_cast<int>((mantissa * 2 - 1) * 1024));
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
		auto library = device->newLibrary(texture_multisample_metallib, texture_multisample_metallib_len);
		report(library != nullptr, "the library loads");

		using Indium::PixelFormat;
		using Indium::TextureType;

		auto makeTexture = [&](TextureType type, PixelFormat format, size_t layers) {
			Indium::TextureDescriptor descriptor {};
			descriptor.textureType = type;
			descriptor.pixelFormat = format;
			descriptor.width = size;
			descriptor.height = size;
			descriptor.depth = 1;
			descriptor.arrayLength = layers;
			descriptor.sampleCount = samples;
			descriptor.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
			descriptor.usage = Indium::TextureUsage::ShaderRead | Indium::TextureUsage::RenderTarget;
			return device->newTexture(descriptor);
		};

		auto shader = [&](const unsigned char* bytes, size_t length, Indium::FunctionType type) {
			Indium::LibraryReflection reflection;
			reflection.functions["main"].functionType = type;
			return device->newLibrary(bytes, length, reflection);
		};

		// clears the texture's layer to `clear` and, when `pipeline` is set, paints the left of the layer with it
		auto render = [&](const std::shared_ptr<Indium::Texture>& texture, size_t layer, Indium::ClearColor clear, const std::shared_ptr<Indium::RenderPipelineState>& pipeline) {
			Indium::RenderPassDescriptor pass {};
			Indium::RenderPassColorAttachmentDescriptor attachment {};
			attachment.texture = texture;
			attachment.slice = layer;
			attachment.loadAction = Indium::LoadAction::Clear;
			attachment.storeAction = Indium::StoreAction::Store;
			attachment.clearColor = clear;
			pass.colorAttachments.push_back(attachment);
			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->renderCommandEncoder(pass);
			if (pipeline) {
				encoder->setRenderPipelineState(pipeline);
				encoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);
			}
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
		};

		auto paintPipeline = [&](PixelFormat format, const unsigned char* fragment, size_t fragmentLength) {
			Indium::RenderPipelineDescriptor descriptor {};
			descriptor.vertexFunction = shader(half_plane_vert, half_plane_vert_len, Indium::FunctionType::Vertex)->newFunction("main");
			descriptor.fragmentFunction = shader(fragment, fragmentLength, Indium::FunctionType::Fragment)->newFunction("main");
			descriptor.rasterSampleCount = samples;
			Indium::RenderPipelineColorAttachmentDescriptor color {};
			color.pixelFormat = format;
			descriptor.colorAttachments.push_back(color);
			return device->newRenderPipelineState(descriptor);
		};

		// runs `kernel` with the texture and a buffer of `outputBytes` (poisoned) and the sample count, over size x size x layers threads
		auto read = [&](const char* kernel, const std::shared_ptr<Indium::Texture>& texture, size_t outputBytes, size_t layers) {
			auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
			if (!pipeline) {
				throw std::runtime_error(std::string(kernel) + " did not load");
			}
			std::vector<uint8_t> poison(outputBytes, 0xA5);
			auto output = device->newBuffer(poison.data(), poison.size(), Indium::ResourceOptions::StorageModeShared);
			uint32_t count = samples;
			auto countBuffer = device->newBuffer(&count, sizeof(count), Indium::ResourceOptions::StorageModeShared);
			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->computeCommandEncoder();
			encoder->setComputePipelineState(pipeline);
			encoder->setTexture(texture, 0);
			encoder->setBuffer(output, 0, 0);
			encoder->setBuffer(countBuffer, 0, 1);
			encoder->dispatchThreads(Indium::Size { size, size, layers }, Indium::Size { 1, 1, 1 });
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
			return std::vector<uint8_t>(static_cast<const uint8_t*>(output->contents()), static_cast<const uint8_t*>(output->contents()) + outputBytes);
		};

		auto check = [&](const char* kernel, const std::string& what, const std::function<bool()>& body) {
			try {
				report(body(), std::string(kernel) + ": " + what);
			} catch (const std::exception& e) {
				report(false, std::string(kernel) + ": " + e.what());
			}
		};

		// every sample of every pixel, as four channels of `channelBytes` bytes: painted samples are (red, 0, 0, 1) and the
		// others the (0, 0, 1, 1) they were cleared to; for integer textures (cleared to zero: the clear color is a
		// floating-point one) the others are (0, 0, 0, 0)
		auto checkColor = [&](const char* kernel, PixelFormat format, size_t channelBytes, const std::function<void(double, uint8_t*)>& encode, double red, bool integer) {
			check(kernel, "every sample of the pixels left, on and right of the edge", [&] {
				auto texture = makeTexture(TextureType::e2DMultisample, format, 1);
				auto pipeline = paintPipeline(format, integer ? paint_uint_frag : paint_frag, integer ? paint_uint_frag_len : paint_frag_len);
				render(texture, 0, integer ? Indium::ClearColor(0, 0, 0, 0) : Indium::ClearColor(0, 0, 1, 1), pipeline);
				auto bytes = read(kernel, texture, size * size * samples * 4 * channelBytes, 1);
				bool ok = true;
				for (size_t y = 0; y < size; ++y) {
					for (size_t x = 0; x < size; ++x) {
						for (size_t s = 0; s < samples; ++s) {
							uint8_t want[16] = {};
							encode(painted(x, s) ? red : 0, want);
							encode(0, want + channelBytes);
							encode(painted(x, s) || integer ? 0 : 1, want + 2 * channelBytes);
							encode(painted(x, s) || !integer ? 1 : 0, want + 3 * channelBytes);
							ok = ok && std::memcmp(bytes.data() + (((y * size + x) * samples + s) * 4) * channelBytes, want, 4 * channelBytes) == 0;
						}
					}
				}
				return ok;
			});
		};

		checkColor("ms_read_float", PixelFormat::RGBA8Unorm, 4, [](double v, uint8_t* out) { float f = v; std::memcpy(out, &f, 4); }, 1, false);
		checkColor("ms_read_half", PixelFormat::RGBA8Unorm, 2, [](double v, uint8_t* out) { uint16_t h = toHalf(static_cast<unsigned>(v)); std::memcpy(out, &h, 2); }, 1, false);
		checkColor("ms_read_uint", PixelFormat::RGBA8Uint, 4, [](double v, uint8_t* out) { uint32_t u = static_cast<uint32_t>(v); std::memcpy(out, &u, 4); }, 200, true);

		check("ms_read_depth", "every sample of a cleared depth texture", [&] {
			auto texture = makeTexture(TextureType::e2DMultisample, PixelFormat::Depth32Float, 1);
			Indium::RenderPassDescriptor pass {};
			Indium::RenderPassDepthAttachmentDescriptor depth {};
			depth.texture = texture;
			depth.loadAction = Indium::LoadAction::Clear;
			depth.storeAction = Indium::StoreAction::Store;
			depth.clearDepth = 0.25;
			pass.depthAttachment = depth;
			auto commandBuffer = queue->commandBuffer();
			commandBuffer->renderCommandEncoder(pass)->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
			auto bytes = read("ms_read_depth", texture, size * size * samples * 4, 1);
			bool ok = true;
			for (size_t i = 0; i < size * size * samples; ++i) {
				float value;
				std::memcpy(&value, bytes.data() + i * 4, 4);
				ok = ok && value == 0.25f;
			}
			return ok;
		});

		// a render pass cannot target a layer other than the first yet, so only the first layer of the array has contents
		check("ms_read_array", "every sample of the first layer of a multisample array", [&] {
			auto texture = makeTexture(TextureType::e2DMultisampleArray, PixelFormat::RGBA8Unorm, 3);
			render(texture, 0, Indium::ClearColor(1, 0, 0, 1), nullptr);
			auto bytes = read("ms_read_array", texture, size * size * samples * 16, 1);
			bool ok = true;
			for (size_t i = 0; i < size * size * samples; ++i) {
				float want[4] = { 1.f, 0.f, 0.f, 1.f };
				ok = ok && std::memcmp(bytes.data() + i * 16, want, 16) == 0;
			}
			return ok;
		});

		check("ms_sizes", "width, height and sample count", [&] {
			auto texture = makeTexture(TextureType::e2DMultisample, PixelFormat::RGBA8Unorm, 1);
			auto bytes = read("ms_sizes", texture, 3 * 4, 1);
			uint32_t values[3];
			std::memcpy(values, bytes.data(), sizeof(values));
			return values[0] == size && values[1] == size && values[2] == samples;
		});

		check("ms_array_sizes", "width, height, layer count and sample count", [&] {
			auto texture = makeTexture(TextureType::e2DMultisampleArray, PixelFormat::RGBA8Unorm, 3);
			auto bytes = read("ms_array_sizes", texture, 4 * 4, 1);
			uint32_t values[4];
			std::memcpy(values, bytes.data(), sizeof(values));
			return values[0] == size && values[1] == size && values[2] == 3 && values[3] == samples;
		});

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
