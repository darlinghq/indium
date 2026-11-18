// SPDX-License-Identifier: 0BSD
#include "viewport-spv.h"
#include <indium/indium.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <thread>
#include <vector>

namespace {
struct Shutdown {
	~Shutdown() { Indium::finit(); }
};

class EventPump {
	std::shared_ptr<Indium::Device> device;
	std::atomic<bool> running { true };
	std::thread thread;
public:
	explicit EventPump(std::shared_ptr<Indium::Device> value) : device(value), thread([this] {
		while (running.load()) device->pollEvents(UINT64_MAX);
	}) {}
	~EventPump() {
		running.store(false);
		device->wakeupEventLoop();
		thread.join();
	}
};

int run() {
	auto device = Indium::createSystemDefaultDevice();
	if (!device) {
		std::puts("SKIP viewport-origin: no Vulkan device");
		return 77;
	}
	std::printf("device: %s\n", device->name().c_str());
	EventPump pump(device);
	auto queue = device->newCommandQueue();
	Indium::LibraryReflection reflection;
	reflection.functions["rectangleVertex"].functionType = Indium::FunctionType::Vertex;
	reflection.functions["rectangleFragment"].functionType = Indium::FunctionType::Fragment;
	std::string error;
	auto library = device->newLibrary(viewport_spv, viewport_spv_len, reflection, &error);
	if (!library) {
		std::fprintf(stderr, "FAIL viewport shader: %s\n", error.c_str());
		return 1;
	}
	Indium::RenderPipelineDescriptor pipelineDescriptor {};
	pipelineDescriptor.vertexFunction = library->newFunction("rectangleVertex");
	pipelineDescriptor.fragmentFunction = library->newFunction("rectangleFragment");
	pipelineDescriptor.colorAttachments.emplace_back();
	pipelineDescriptor.colorAttachments[0].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
	auto pipeline = device->newRenderPipelineState(pipelineDescriptor);
	constexpr size_t width = 32, height = 24;
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, width, height, false);
	descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
	auto target = device->newTexture(descriptor);
	struct Case { Indium::Viewport metal; VkViewport vulkan; };
	// Vulkan's negative-height viewport starts at the lower-left corner.
	const Case cases[] = {
		{ { 0, 0, 12, 8, 0, 1 }, { 0, 8, 12, -8, 0, 1 } },
		{ { 4, 7, 12, 9, 0, 1 }, { 4, 16, 12, -9, 0, 1 } },
		{ { 2.25, 5.25, 11.5, 8.5, 0, 1 }, { 2.25f, 13.75f, 11.5f, -8.5f, 0, 1 } },
		{ { -3, -2, 12, 10, 0, 1 }, { -3, 8, 12, -10, 0, 1 } },
		{ { 22, 16, 10, 8, 0, 1 }, { 22, 24, 10, -8, 0, 1 } },
	};
	auto draw = [&](const Case& test, int method) {
		const auto& viewport = test.metal;
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.emplace_back();
		pass.colorAttachments[0].texture = target;
		pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
		pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
		pass.colorAttachments[0].clearColor = Indium::ClearColor(0, 0, 0, 1);
		auto command = queue->commandBuffer();
		auto encoder = command->renderCommandEncoder(pass);
		encoder->setRenderPipelineState(pipeline);
		if (method == -1) {
			// Independent reference: bypass Indium's viewport conversion, using
			// the explicitly specified native Vulkan rectangle above. In particular,
			// do not assume fractional viewport edges cover identical samples on
			// different drivers. Every pixel still has to match the reference.
			auto native = std::dynamic_pointer_cast<Indium::PrivateCommandBuffer>(command);
			Indium::DynamicVK::vkCmdSetViewportWithCount(native->commandBuffer(), 1, &test.vulkan);
		} else if (method == 0) encoder->setViewport(viewport);
		else if (method == 1) encoder->setViewports(&viewport, 1);
		else encoder->setViewports(std::vector<Indium::Viewport> { viewport });
		encoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 6);
		encoder->endEncoding();
		command->commit();
		command->waitUntilCompleted();
		std::vector<uint32_t> pixels(width * height);
		target->getBytes(Indium::Region::make2D(0, 0, width, height), 0, pixels.data(), width * 4);
		return pixels;
	};
	const char* methods[] = { "setViewport", "setViewports(pointer)", "setViewports(vector)" };
	unsigned failures = 0;
	for (const auto& test : cases) {
		const auto& viewport = test.metal;
		auto reference = draw(test, -1);
		bool integral = std::floor(viewport.originX) == viewport.originX && std::floor(viewport.originY) == viewport.originY &&
			std::floor(viewport.width) == viewport.width && std::floor(viewport.height) == viewport.height;
		unsigned referenceErrors = 0;
		for (size_t y = 0; y < height; ++y) for (size_t x = 0; x < width; ++x) {
			bool inside = x + 0.5 >= viewport.originX && x + 0.5 < viewport.originX + viewport.width &&
				y + 0.5 >= viewport.originY && y + 0.5 < viewport.originY + viewport.height;
			auto pixel = reference[y * width + x];
			referenceErrors += integral ? pixel != (inside ? 0xff0000ffu : 0xff000000u) :
				pixel != 0xff0000ffu && pixel != 0xff000000u;
		}
		auto red = std::count(reference.begin(), reference.end(), 0xff0000ffu);
		referenceErrors += red == 0 || red == width * height;
		if (referenceErrors) {
			std::printf("FAIL native reference origin=(%.2f,%.2f): %u errors\n", viewport.originX, viewport.originY, referenceErrors);
			++failures;
			continue;
		}
		for (unsigned method = 0; method < 3; ++method) {
			auto pixels = draw(test, method);
			unsigned mismatches = 0;
			for (size_t y = 0; y < height; ++y) for (size_t x = 0; x < width; ++x) {
				bool mismatch = pixels[y * width + x] != reference[y * width + x];
				if (mismatch && mismatches < 4) std::printf("     pixel (%zu,%zu): %08x, expected %08x\n", x, y, pixels[y * width + x], reference[y * width + x]);
				mismatches += mismatch;
			}
			std::printf("%s %s origin=(%.2f,%.2f), extent=(%.2f,%.2f): %u pixel mismatches\n",
				mismatches ? "FAIL" : "PASS", methods[method], viewport.originX, viewport.originY,
				viewport.width, viewport.height, mismatches);
			failures += mismatches != 0;
		}
	}
	return failures ? 1 : 0;
}
}

int main() {
	Indium::init(nullptr, 0, false);
	Shutdown shutdown;
	try {
		return run();
	} catch (const std::exception& error) {
		std::fprintf(stderr, "FAIL viewport-origin: %s\n", error.what());
		return 1;
	}
}
