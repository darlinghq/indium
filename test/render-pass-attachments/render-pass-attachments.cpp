// Copyright (C) 2026 Darling Developers
//
// Render pass attachment handling, without drawing:
//  - an attachment is only discarded by a Clear; after a DontCare pass its contents are whatever
//    the earlier pass left (Metal leaves them undefined, so preserving them is conforming);
//  - a pass may have only a depth attachment, or a gap in its color attachments;
//  - depth and stencil may be attached together from one combined-format texture.

#include <indium/indium.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

std::shared_ptr<Indium::Texture> newTarget(Indium::Device* device, Indium::PixelFormat format) {
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(format, 4, 4, false);
	descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
	return device->newTexture(descriptor);
}

// Runs one render pass with no draws. Returns false if Indium refused the pass.
bool runPass(Indium::CommandQueue* queue, const Indium::RenderPassDescriptor& descriptor, const char* what) {
	try {
		auto commandBuffer = queue->commandBuffer();
		auto encoder = commandBuffer->renderCommandEncoder(descriptor);
		encoder->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		return true;
	} catch (const std::exception& e) {
		std::printf("     %s: %s\n", what, e.what());
		return false;
	}
}

Indium::RenderPassColorAttachmentDescriptor colorAttachment(std::shared_ptr<Indium::Texture> texture, Indium::LoadAction loadAction, Indium::StoreAction storeAction) {
	Indium::RenderPassColorAttachmentDescriptor attachment {};
	attachment.texture = texture;
	attachment.loadAction = loadAction;
	attachment.storeAction = storeAction;
	attachment.clearColor = Indium::ClearColor(1, 0, 0, 1);
	return attachment;
}

bool allPixels(Indium::Texture* texture, uint32_t rgba) {
	std::vector<uint32_t> pixels(16, 0);
	texture->getBytes(Indium::Region::make2D(0, 0, 4, 4), 0, pixels.data(), 4 * 4);
	for (auto pixel: pixels) {
		if (pixel != rgba) {
			std::printf("     pixel %08x, expected %08x\n", pixel, rgba);
			return false;
		}
	}
	return true;
}

}

int main() {
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
		constexpr uint32_t red = 0xff0000ff; // RGBA8 (1, 0, 0, 1), little endian

		auto color = newTarget(device.get(), Indium::PixelFormat::RGBA8Unorm);
		Indium::RenderPassDescriptor clear {};
		clear.colorAttachments.push_back(colorAttachment(color, Indium::LoadAction::Clear, Indium::StoreAction::Store));
		report(runPass(queue.get(), clear, "clear") && allPixels(color.get(), red), "a Clear pass writes the clear color");

		Indium::RenderPassDescriptor dontCare {};
		dontCare.colorAttachments.push_back(colorAttachment(color, Indium::LoadAction::DontCare, Indium::StoreAction::DontCare));
		report(runPass(queue.get(), dontCare, "dont care") && allPixels(color.get(), red), "a DontCare pass leaves the contents alone");

		Indium::RenderPassDescriptor gap {};
		gap.colorAttachments.push_back(Indium::RenderPassColorAttachmentDescriptor {});
		gap.colorAttachments.push_back(colorAttachment(color, Indium::LoadAction::Load, Indium::StoreAction::Store));
		report(runPass(queue.get(), gap, "gap") && allPixels(color.get(), red), "a pass with an unused color attachment slot");

		Indium::RenderPassDescriptor depthOnly {};
		depthOnly.depthAttachment = Indium::RenderPassDepthAttachmentDescriptor {};
		depthOnly.depthAttachment->texture = newTarget(device.get(), Indium::PixelFormat::Depth32Float);
		depthOnly.depthAttachment->loadAction = Indium::LoadAction::Clear;
		depthOnly.depthAttachment->storeAction = Indium::StoreAction::Store;
		depthOnly.depthAttachment->clearDepth = 0.5;
		report(runPass(queue.get(), depthOnly, "depth only"), "a pass with only a depth attachment");

		auto depthStencil = newTarget(device.get(), Indium::PixelFormat::Depth32Float_Stencil8);
		Indium::RenderPassDescriptor combined {};
		combined.colorAttachments.push_back(colorAttachment(color, Indium::LoadAction::Load, Indium::StoreAction::Store));
		combined.depthAttachment = Indium::RenderPassDepthAttachmentDescriptor {};
		combined.depthAttachment->texture = depthStencil;
		combined.depthAttachment->loadAction = Indium::LoadAction::Clear;
		combined.depthAttachment->clearDepth = 1.0;
		combined.stencilAttachment = Indium::RenderPassStencilAttachmentDescriptor {};
		combined.stencilAttachment->texture = depthStencil;
		combined.stencilAttachment->loadAction = Indium::LoadAction::Clear;
		combined.stencilAttachment->clearStencil = 0x80;
		report(runPass(queue.get(), combined, "depth and stencil"), "depth and stencil from one texture");

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
