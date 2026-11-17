// Copyright (C) 2026 Lubos Dolezel
//
// CommandBuffer::parallelRenderCommandEncoder: the encoders of one pass can be used from any thread and in any
// order; their commands run in the order the encoders were made, each starting from the default state, in one pass
// whose load and store actions apply once. Every check compares what a serial pass with one encoder makes with
// what the parallel pass makes.

#include "graphics-spv.h"

#include <indium/indium.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

using namespace Indium;

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	std::fflush(stdout);
	failures += !ok;
}

constexpr size_t size = 8;
using Pixels = std::array<uint32_t, size * size>;

std::shared_ptr<Device> device;
std::shared_ptr<CommandQueue> queue;
std::shared_ptr<RenderPipelineState> plain;     // writes red
std::shared_ptr<RenderPipelineState> blendColor; // writes the blend color

struct Target {
	std::shared_ptr<Texture> texture;
	RenderPassDescriptor pass {};
};

// a pass that clears a new target to opaque blue
Target makeTarget() {
	Target target;
	auto descriptor = TextureDescriptor::texture2DDescriptor(PixelFormat::RGBA8Unorm, size, size, false);
	descriptor.usage = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
	target.texture = device->newTexture(descriptor);
	target.pass.colorAttachments.emplace_back();
	auto& color = target.pass.colorAttachments.back();
	color.texture = target.texture;
	color.loadAction = LoadAction::Clear;
	color.storeAction = StoreAction::Store;
	color.clearColor = ClearColor(0, 0, 1, 1);
	return target;
}

// the target's pixels after `encode` recorded a pass on it
Pixels run(Target& target, const std::function<void(CommandBuffer&)>& encode) {
	auto commands = queue->commandBuffer();
	encode(*commands);
	auto readback = device->newBuffer(sizeof(Pixels), ResourceOptions::StorageModeShared);
	auto blit = commands->blitCommandEncoder();
	blit->copy(target.texture, 0, 0, Origin { 0, 0, 0 }, Size { size, size, 1 }, readback, 0, size * 4, size * size * 4);
	blit->endEncoding();
	commands->commit();
	commands->waitUntilCompleted();
	Pixels pixels;
	std::memcpy(pixels.data(), readback->contents(), sizeof(Pixels));
	return pixels;
}

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
	return r | (g << 8) | (b << 16) | (static_cast<uint32_t>(a) << 24);
}

bool allEqual(const Pixels& pixels, uint32_t value) {
	for (auto pixel: pixels) {
		if (pixel != value) {
			return false;
		}
	}
	return true;
}

// the draw that covers the target (see test/hazard-tracking/graphics.metal)
void drawAll(RenderCommandEncoder& encoder, const std::shared_ptr<RenderPipelineState>& pipeline) {
	encoder.setRenderPipelineState(pipeline);
	encoder.drawPrimitives(PrimitiveType::Triangle, 0, 3);
}

void drawWithBlendColor(RenderCommandEncoder& encoder, float value) {
	encoder.setBlendColor(value, value, value, 1);
	drawAll(encoder, blendColor);
}

std::shared_ptr<Library> loadLibrary() {
	LibraryReflection reflection;
	reflection.functions["triangleVertex"].functionType = FunctionType::Vertex;
	reflection.functions["redFragment"].functionType = FunctionType::Fragment;
	std::string error;
	auto library = device->newLibrary(graphics_spv, graphics_spv_len, reflection, &error);
	if (!library) {
		std::printf("newLibrary: %s\n", error.c_str());
	}
	return library;
}

void run() {
	// --- a parallel pass without encoders is a pass: the load action ran, nothing was drawn
	{
		auto target = makeTarget();
		auto pixels = run(target, [&](CommandBuffer& commands) {
			commands.parallelRenderCommandEncoder(target.pass)->endEncoding();
		});
		report(allEqual(pixels, rgba(0, 0, 255)), "a parallel pass without encoders only clears");
	}

	// --- one encoder: the same as a serial pass
	{
		auto serial = makeTarget();
		auto expected = run(serial, [&](CommandBuffer& commands) {
			auto encoder = commands.renderCommandEncoder(serial.pass);
			drawAll(*encoder, plain);
			encoder->endEncoding();
		});
		auto parallel = makeTarget();
		auto pixels = run(parallel, [&](CommandBuffer& commands) {
			auto pass = commands.parallelRenderCommandEncoder(parallel.pass);
			auto encoder = pass->renderCommandEncoder();
			drawAll(*encoder, plain);
			encoder->endEncoding();
			pass->endEncoding();
		});
		report(allEqual(expected, rgba(255, 0, 0)) && pixels == expected, "one encoder gives what a serial pass gives");
	}

	// --- commands run in the order the encoders were made, not the order they were used or ended in
	{
		auto serial = makeTarget();
		auto expected = run(serial, [&](CommandBuffer& commands) {
			auto encoder = commands.renderCommandEncoder(serial.pass);
			drawWithBlendColor(*encoder, 0.2f);
			drawWithBlendColor(*encoder, 0.6f);
			encoder->endEncoding();
		});
		auto parallel = makeTarget();
		auto pixels = run(parallel, [&](CommandBuffer& commands) {
			auto pass = commands.parallelRenderCommandEncoder(parallel.pass);
			auto first = pass->renderCommandEncoder();
			auto second = pass->renderCommandEncoder();
			// the second encoder is used and ended before the first one is touched
			drawWithBlendColor(*second, 0.6f);
			second->endEncoding();
			drawWithBlendColor(*first, 0.2f);
			first->endEncoding();
			pass->endEncoding();
		});
		report(allEqual(expected, rgba(153, 0, 0)) && pixels == expected, "encoders run in creation order, whatever the order of use");
	}

	// --- an encoder starts from the default state, not from what the one before it left
	{
		auto parallel = makeTarget();
		auto pixels = run(parallel, [&](CommandBuffer& commands) {
			auto pass = commands.parallelRenderCommandEncoder(parallel.pass);
			auto first = pass->renderCommandEncoder();
			first->setScissorRect(ScissorRect { size, size / 2, 0, 0 });
			drawWithBlendColor(*first, 0.5f);
			first->endEncoding();
			auto second = pass->renderCommandEncoder();
			// no scissor rectangle or blend color: the whole target, with a zero blend color
			drawAll(*second, blendColor);
			second->endEncoding();
			pass->endEncoding();
		});
		report(allEqual(pixels, rgba(0, 0, 0, 0)), "an encoder starts with the default scissor rectangle and blend color");
	}

	// --- the encoders can be used from threads at the same time
	{
		constexpr size_t count = 8;
		auto serial = makeTarget();
		auto expected = run(serial, [&](CommandBuffer& commands) {
			auto encoder = commands.renderCommandEncoder(serial.pass);
			for (size_t i = 0; i < count; ++i) {
				encoder->setScissorRect(ScissorRect { size, 1, i, 0 });
				drawWithBlendColor(*encoder, static_cast<float>(i + 1) / 10);
				// the encoder after this one starts with the defaults
				encoder->setScissorRect(ScissorRect { size, size, 0, 0 });
			}
			encoder->endEncoding();
		});
		auto parallel = makeTarget();
		auto pixels = run(parallel, [&](CommandBuffer& commands) {
			auto pass = commands.parallelRenderCommandEncoder(parallel.pass);
			std::vector<std::shared_ptr<RenderCommandEncoder>> encoders;
			for (size_t i = 0; i < count; ++i) {
				encoders.push_back(pass->renderCommandEncoder());
			}
			std::vector<std::thread> threads;
			for (size_t i = count; i-- > 0;) {
				threads.emplace_back([&, i] {
					encoders[i]->setScissorRect(ScissorRect { size, 1, i, 0 });
					drawWithBlendColor(*encoders[i], static_cast<float>(i + 1) / 10);
					encoders[i]->endEncoding();
				});
			}
			for (auto& thread: threads) {
				thread.join();
			}
			pass->endEncoding();
		});
		report(pixels == expected && pixels[1] == rgba(51, 0, 0) && pixels[count - 1] == rgba(204, 0, 0), "eight encoders recorded on eight threads give what a serial pass gives");
	}

	// --- a pass with a store action set on the parallel encoder, and the target kept
	{
		auto target = makeTarget();
		target.pass.colorAttachments[0].storeAction = StoreAction::Unknown;
		auto pixels = run(target, [&](CommandBuffer& commands) {
			auto pass = commands.parallelRenderCommandEncoder(target.pass);
			pass->setColorStoreAction(StoreAction::Store, 0);
			// an attachment that does not exist is ignored
			pass->setColorStoreAction(StoreAction::Store, 5);
			pass->setDepthStoreAction(StoreAction::Store);
			auto encoder = pass->renderCommandEncoder();
			drawAll(*encoder, plain);
			encoder->endEncoding();
			pass->endEncoding();
		});
		report(allEqual(pixels, rgba(255, 0, 0)), "store actions set on the parallel encoder keep the result");
	}

	// --- a sub-encoder asked for after the pass ended is nothing, and ending twice does nothing
	{
		auto target = makeTarget();
		std::shared_ptr<RenderCommandEncoder> late;
		auto pixels = run(target, [&](CommandBuffer& commands) {
			auto pass = commands.parallelRenderCommandEncoder(target.pass);
			auto encoder = pass->renderCommandEncoder();
			drawAll(*encoder, plain);
			encoder->endEncoding();
			pass->endEncoding();
			late = pass->renderCommandEncoder();
			pass->endEncoding();
		});
		report(late == nullptr && allEqual(pixels, rgba(255, 0, 0)), "after the pass ended no encoder is made and ending again does nothing");
	}
}

} // namespace

int main() {
	Indium::init(nullptr, 0, false);
	{
		auto systemDevice = createSystemDefaultDevice();
		if (!systemDevice) {
			std::printf("SKIP no Vulkan device\n");
			Indium::finit();
			return 0;
		}
		device = systemDevice;
		queue = device->newCommandQueue();

		std::atomic<bool> keepPolling { true };
		std::thread poller([&]() {
			while (keepPolling) {
				device->pollEvents(UINT64_MAX);
			}
		});

		auto library = loadLibrary();
		if (!library) {
			report(false, "the module loads");
		} else {
			RenderPipelineDescriptor descriptor {};
			descriptor.vertexFunction = library->newFunction("triangleVertex");
			descriptor.fragmentFunction = library->newFunction("redFragment");
			descriptor.colorAttachments.emplace_back();
			descriptor.colorAttachments[0].pixelFormat = PixelFormat::RGBA8Unorm;
			plain = device->newRenderPipelineState(descriptor);
			// source color times the blend color: a draw writes the blend color's value in the red channel, whatever was there
			auto& color = descriptor.colorAttachments[0];
			color.blendingEnabled = true;
			color.sourceRGBBlendFactor = BlendFactor::BlendColor;
			color.destinationRGBBlendFactor = BlendFactor::Zero;
			color.sourceAlphaBlendFactor = BlendFactor::BlendColor;
			color.destinationAlphaBlendFactor = BlendFactor::Zero;
			blendColor = device->newRenderPipelineState(descriptor);
			report(plain && blendColor, "the pipelines are made");
			if (plain && blendColor) {
				run();
			}
		}

		keepPolling = false;
		device->wakeupEventLoop();
		poller.join();
		plain.reset();
		blendColor.reset();
		library.reset();
		queue.reset();
		device.reset();
		systemDevice.reset();
	}
	Indium::finit();
	std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
