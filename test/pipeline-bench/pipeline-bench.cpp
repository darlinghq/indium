// Copyright (C) 2026 Lubos Dolezel
//
// Times the steps that make up pipeline creation: translating a library from AIR, creating the pipeline
// states, and the first use of a pipeline state, which is where the Vulkan pipelines are made. Run it
// twice in a row against one INDIUM_PIPELINE_CACHE_DIR to see the effect of the on-disk caches.

#include "compute-lib.h"
#include "lighting-lib.h"

#include <indium/indium.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

}

static int run() {
	Indium::init(nullptr, 0, false);
	auto device = Indium::createSystemDefaultDevice();
	if (!device) {
		std::printf("no device\n");
		return 77;
	}

	// the logical device is created lazily; do it outside the timed steps
	auto queue = device->newCommandQueue();

	std::atomic<bool> polling { true };
	std::thread poller([&] {
		while (polling) {
			device->pollEvents(UINT64_MAX);
		}
	});

	auto start = Clock::now();
	auto lighting = device->newLibrary(lighting_lib, lighting_lib_len);
	std::printf("newLibrary(render) %.3f ms\n", millisecondsSince(start));

	start = Clock::now();
	auto computeLibrary = device->newLibrary(compute_lib, compute_lib_len);
	std::printf("newLibrary(compute) %.3f ms\n", millisecondsSince(start));
	if (!lighting || !computeLibrary) {
		return 1;
	}

	start = Clock::now();
	Indium::RenderPipelineDescriptor descriptor {};
	descriptor.vertexFunction = lighting->newFunction("vertex_project");
	descriptor.fragmentFunction = lighting->newFunction("fragment_light");
	descriptor.colorAttachments.emplace_back();
	descriptor.colorAttachments[0].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
	descriptor.depthAttachmentPixelFormat = Indium::PixelFormat::Depth32Float;
	auto renderState = device->newRenderPipelineState(descriptor);
	std::printf("newRenderPipelineState %.3f ms\n", millisecondsSince(start));

	start = Clock::now();
	auto computeState = device->newComputePipelineState(computeLibrary->newFunction("add_arrays"));
	std::printf("newComputePipelineState %.3f ms\n", millisecondsSince(start));

	auto colorDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 64, 64, false);
	colorDescriptor.usage = Indium::TextureUsage::RenderTarget;
	auto color = device->newTexture(colorDescriptor);
	auto depthDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::Depth32Float, 64, 64, false);
	depthDescriptor.usage = Indium::TextureUsage::RenderTarget;
	auto depth = device->newTexture(depthDescriptor);

	start = Clock::now();
	{
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.emplace_back();
		pass.colorAttachments[0].texture = color;
		pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
		pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
		pass.depthAttachment = Indium::RenderPassDepthAttachmentDescriptor {};
		pass.depthAttachment->texture = depth;
		pass.depthAttachment->loadAction = Indium::LoadAction::Clear;
		pass.depthAttachment->storeAction = Indium::StoreAction::Store;
		auto commandBuffer = queue->commandBuffer();
		auto encoder = commandBuffer->renderCommandEncoder(pass);
		encoder->setRenderPipelineState(renderState);
		encoder->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
	}
	std::printf("first use of the render pipeline state %.3f ms\n", millisecondsSince(start));

	for (const char* use: { "first", "second" }) {
		start = Clock::now();
		auto buffer = device->newBuffer(4096, Indium::ResourceOptions::StorageModeShared);
		auto commandBuffer = queue->commandBuffer();
		auto encoder = commandBuffer->computeCommandEncoder();
		encoder->setComputePipelineState(computeState);
		encoder->setBuffer(buffer, 0, 0);
		encoder->setBuffer(buffer, 0, 1);
		encoder->setBuffer(buffer, 0, 2);
		encoder->dispatchThreads(Indium::Size { 64, 1, 1 }, Indium::Size { 64, 1, 1 });
		encoder->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		std::printf("%s use of the compute pipeline state %.3f ms\n", use, millisecondsSince(start));
	}
	polling = false;
	device->wakeupEventLoop();
	poller.join();
	return 0;
}

// every texture and device is released when run() returns, before the Vulkan instance goes
int main() {
	int result = run();
	Indium::finit();
	return result;
}
