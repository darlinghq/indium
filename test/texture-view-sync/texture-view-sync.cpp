// Copyright (C) 2026 Darling Developers
//
// Texture views share their texture's storage:
//  - one command buffer may use a texture and a view of it (here: render into the view, then
//    copy out of the texture) and must still complete, with the copy seeing the render;
//  - a view of a view of a view addresses the slice its whole chain of ranges selects.

#include <indium/indium.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	std::fflush(stdout);
	failures += !ok;
}

Indium::RenderPassDescriptor clearPass(std::shared_ptr<Indium::Texture> texture, double red, double green) {
	Indium::RenderPassDescriptor pass {};
	pass.colorAttachments.emplace_back();
	pass.colorAttachments.back().texture = texture;
	pass.colorAttachments.back().loadAction = Indium::LoadAction::Clear;
	pass.colorAttachments.back().storeAction = Indium::StoreAction::Store;
	pass.colorAttachments.back().clearColor = Indium::ClearColor(red, green, 0, 1);
	return pass;
}

}

int main() {
	// a submission that waits for itself never completes
	std::thread([]() {
		std::this_thread::sleep_for(std::chrono::seconds(30));
		std::printf("FAIL timed out\n");
		std::fflush(stdout);
		_exit(1);
	}).detach();

	Indium::init(nullptr, 0, false);

	{
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			return 0;
		}

		bool keepPollingDevice = true;
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		auto queue = device->newCommandQueue();
		constexpr uint32_t red = 0xff0000ff;
		constexpr uint32_t green = 0xff00ff00;

		{
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
			descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
			auto texture = device->newTexture(descriptor);
			auto view = texture->newTextureView(Indium::PixelFormat::RGBA8Unorm);
			auto readback = device->newBuffer(4 * 4 * 4, Indium::ResourceOptions::StorageModeShared);

			auto commandBuffer = queue->commandBuffer();
			auto render = commandBuffer->renderCommandEncoder(clearPass(view, 1, 0));
			render->endEncoding();
			auto blit = commandBuffer->blitCommandEncoder();
			blit->copy(texture, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { 4, 4, 1 }, readback, 0, 4 * 4, 4 * 4 * 4);
			blit->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();

			auto pixels = static_cast<const uint32_t*>(readback->contents());
			bool ok = true;
			for (size_t i = 0; i < 16; ++i) {
				ok = ok && pixels[i] == red;
			}
			report(ok, "a texture and its view in one command buffer");
		}

		{
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
			descriptor.textureType = Indium::TextureType::e2DArray;
			descriptor.arrayLength = 4;
			descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
			auto texture = device->newTexture(descriptor);
			auto slices1to3 = texture->newTextureView(Indium::PixelFormat::RGBA8Unorm, Indium::TextureType::e2DArray, Indium::Range<size_t> { 0, 1 }, Indium::Range<size_t> { 1, 3 });
			auto slices2to3 = slices1to3->newTextureView(Indium::PixelFormat::RGBA8Unorm, Indium::TextureType::e2DArray, Indium::Range<size_t> { 0, 1 }, Indium::Range<size_t> { 1, 2 });
			auto slice3 = slices2to3->newTextureView(Indium::PixelFormat::RGBA8Unorm, Indium::TextureType::e2D, Indium::Range<size_t> { 0, 1 }, Indium::Range<size_t> { 1, 1 });

			auto commandBuffer = queue->commandBuffer();
			for (size_t slice = 0; slice < 4; ++slice) {
				auto single = texture->newTextureView(Indium::PixelFormat::RGBA8Unorm, Indium::TextureType::e2D, Indium::Range<size_t> { 0, 1 }, Indium::Range<size_t> { slice, 1 });
				auto render = commandBuffer->renderCommandEncoder(clearPass(single, 0, 1));
				render->endEncoding();
			}
			auto render = commandBuffer->renderCommandEncoder(clearPass(slice3, 1, 0));
			render->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();

			uint32_t expected[4] = { green, green, green, red };
			bool ok = true;
			for (size_t slice = 0; slice < 4; ++slice) {
				std::vector<uint32_t> pixels(16, 0);
				texture->getBytes(Indium::Region::make2D(0, 0, 4, 4), 0, slice, pixels.data(), 4 * 4, 4 * 4 * 4);
				if (pixels[0] != expected[slice]) {
					std::printf("     slice %zu reads %08x\n", slice, pixels[0]);
					ok = false;
				}
			}
			report(ok, "a view of a view of a view selects the right slice");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
