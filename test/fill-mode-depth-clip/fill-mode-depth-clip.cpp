// Copyright (C) 2026 Lubos Dolezel
//
// The fill mode and depth clip mode of a render encoder apply to the draws that follow them:
//  - TriangleFillMode::Lines draws the outline of a triangle instead of its interior, Fill goes back;
//  - DepthClipMode::Clamp draws a triangle that lies beyond the far plane (depth is clamped to the viewport range),
//    Clip, the default, discards it;
//  - both persist across a pipeline change and a new encoder starts with the defaults.
// The shaders are mslc's output for graphics.metal in this directory.
// Prints SKIP and exits 0 without a Vulkan device.

#include "graphics-spv.h"

#include <indium/indium.hpp>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t size = 16;

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

		Indium::LibraryReflection reflection;
		reflection.functions["smallVertex"].functionType = Indium::FunctionType::Vertex;
		reflection.functions["farVertex"].functionType = Indium::FunctionType::Vertex;
		reflection.functions["redFragment"].functionType = Indium::FunctionType::Fragment;
		std::string error;
		auto library = device->newLibrary(graphics_spv, graphics_spv_len, reflection, &error);
		report(library != nullptr, "the module loads");
		if (library) {
			auto makePipeline = [&](const char* vertex) {
				Indium::RenderPipelineDescriptor descriptor {};
				descriptor.vertexFunction = library->newFunction(vertex);
				descriptor.fragmentFunction = library->newFunction("redFragment");
				descriptor.colorAttachments.emplace_back();
				descriptor.colorAttachments[0].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
				return device->newRenderPipelineState(descriptor);
			};
			auto small = makePipeline("smallVertex");
			auto far = makePipeline("farVertex");

			auto textureDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, size, size, false);
			textureDescriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
			auto target = device->newTexture(textureDescriptor);

			struct Result {
				std::vector<uint32_t> pixels;
				size_t red = 0;
				bool at(size_t x, size_t y) const { return pixels[y * size + x] == 0xFF0000FFu; }
			};

			// runs the draws on a cleared target and reads the pixels back
			auto run = [&](const std::function<void(Indium::RenderCommandEncoder&)>& body) {
				Indium::RenderPassDescriptor pass {};
				pass.colorAttachments.emplace_back();
				pass.colorAttachments[0].texture = target;
				pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
				pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
				pass.colorAttachments[0].clearColor = Indium::ClearColor(0, 0, 0, 0);
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->renderCommandEncoder(pass);
				body(*encoder);
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				Result result;
				result.pixels.resize(size * size);
				target->getBytes(Indium::Region::make2D(0, 0, size, size), 0, result.pixels.data(), size * 4);
				for (auto pixel : result.pixels) {
					result.red += pixel == 0xFF0000FFu;
				}
				return result;
			};
			auto draw = [](Indium::RenderCommandEncoder& encoder) {
				encoder.drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);
			};

			// --- fill mode
			auto filled = run([&](auto& e) { e.setRenderPipelineState(small); draw(e); });
			report(filled.red > size * size / 4 && filled.at(size / 2, size / 2 - 1), "a triangle is filled by default");

			auto outline = run([&](auto& e) {
				e.setRenderPipelineState(small);
				e.setTriangleFillMode(Indium::TriangleFillMode::Lines);
				draw(e);
			});
			report(outline.red > 0 && outline.red < filled.red / 2, "the Lines fill mode draws fewer pixels than Fill");
			report(!outline.at(size / 2, size / 2 - 1) && !outline.at(size / 2 + 1, size / 2 - 2), "the Lines fill mode leaves the interior empty");

			auto back = run([&](auto& e) {
				e.setRenderPipelineState(small);
				e.setTriangleFillMode(Indium::TriangleFillMode::Lines);
				draw(e);
				e.setTriangleFillMode(Indium::TriangleFillMode::Fill);
				draw(e);
			});
			// the outline drawn first may touch pixels beside the filled shape, so this is a superset of the filled draw
			report(back.red >= filled.red && back.at(size / 2, size / 2 - 1), "setting Fill again fills the following draws");

			auto beforePipeline = run([&](auto& e) {
				e.setTriangleFillMode(Indium::TriangleFillMode::Lines);
				e.setRenderPipelineState(small);
				draw(e);
			});
			report(beforePipeline.red == outline.red, "the fill mode set before the pipeline state applies to it");

			auto acrossPipelines = run([&](auto& e) {
				e.setRenderPipelineState(far);
				e.setTriangleFillMode(Indium::TriangleFillMode::Lines);
				e.setRenderPipelineState(small);
				draw(e);
			});
			report(acrossPipelines.red == outline.red, "the fill mode survives a change of pipeline state");

			auto fresh = run([&](auto& e) { e.setRenderPipelineState(small); draw(e); });
			report(fresh.red == filled.red, "a new encoder starts with the Fill mode");

			// --- depth clip mode
			auto clipped = run([&](auto& e) { e.setRenderPipelineState(far); draw(e); });
			report(clipped.red == 0, "a triangle beyond the far plane is clipped by default");

			auto clamped = run([&](auto& e) {
				e.setRenderPipelineState(far);
				e.setDepthClipMode(Indium::DepthClipMode::Clamp);
				draw(e);
			});
			report(clamped.red == size * size, "the Clamp depth clip mode draws a triangle beyond the far plane");

			auto clippedAgain = run([&](auto& e) {
				e.setRenderPipelineState(far);
				e.setDepthClipMode(Indium::DepthClipMode::Clamp);
				draw(e);
				e.setDepthClipMode(Indium::DepthClipMode::Clip);
				draw(e);
			});
			report(clippedAgain.red == size * size, "Clip after Clamp keeps what was drawn and draws nothing more");

			auto clippedLast = run([&](auto& e) {
				e.setRenderPipelineState(far);
				e.setDepthClipMode(Indium::DepthClipMode::Clamp);
				e.setDepthClipMode(Indium::DepthClipMode::Clip);
				draw(e);
			});
			report(clippedLast.red == 0, "the last depth clip mode set applies");

			// --- both together
			auto both = run([&](auto& e) {
				e.setRenderPipelineState(far);
				e.setDepthClipMode(Indium::DepthClipMode::Clamp);
				e.setTriangleFillMode(Indium::TriangleFillMode::Lines);
				draw(e);
			});
			report(both.red > 0 && both.red < size * size, "Lines and Clamp together draw the outline of the triangle beyond the far plane");

			// the other primitive types are not changed by the fill mode
			auto lines = run([&](auto& e) {
				e.setRenderPipelineState(small);
				e.setTriangleFillMode(Indium::TriangleFillMode::Lines);
				e.drawPrimitives(Indium::PrimitiveType::Line, 0, 2);
			});
			auto linesFill = run([&](auto& e) {
				e.setRenderPipelineState(small);
				e.drawPrimitives(Indium::PrimitiveType::Line, 0, 2);
			});
			report(lines.red == linesFill.red && lines.red > 0, "the fill mode does not change how a line is drawn");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
