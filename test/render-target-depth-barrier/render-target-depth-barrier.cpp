// Copyright (C) 2026 Lubos Dolezel
//
// A render-target memoryBarrier orders depth writes before later reads of the depth in the same pass. See README.md.
// Prints SKIP and exits 0 without a Vulkan device.

#include "graphics-spv.h"

#include <indium/indium.hpp>

#include <cstdint>
#include <limits>
#include <cstdio>
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
		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});
		auto queue = device->newCommandQueue();

		Indium::LibraryReflection reflection;
		reflection.functions["coverVertex"].functionType = Indium::FunctionType::Vertex;
		reflection.functions["redFragment"].functionType = Indium::FunctionType::Fragment;
		reflection.functions["depthReadFragment"].functionType = Indium::FunctionType::Fragment;
		// the depth texture and its sampler: bindings 0 and 1 of the fragment stage's descriptor set (mslc --reflect)
		constexpr size_t noIndex = std::numeric_limits<size_t>::max();
		auto& depthRead = reflection.functions["depthReadFragment"];
		depthRead.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Sample });
		depthRead.bindings.push_back(Indium::BindingDescriptor { 0, 1, noIndex, Indium::BindingType::Sampler, Indium::TextureAccessType::Sample });
		auto library = device->newLibrary(graphics_spv, graphics_spv_len, reflection, nullptr);
		report(library != nullptr, "the module loads");
		if (library) {
			auto makePipeline = [&](const char* fragment) {
				Indium::RenderPipelineDescriptor descriptor {};
				descriptor.vertexFunction = library->newFunction("coverVertex");
				descriptor.fragmentFunction = library->newFunction(fragment);
				descriptor.colorAttachments.emplace_back();
				descriptor.colorAttachments[0].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
				descriptor.depthAttachmentPixelFormat = Indium::PixelFormat::Depth32Float;
				return device->newRenderPipelineState(descriptor);
			};
			auto writePipeline = makePipeline("redFragment");
			auto readPipeline = makePipeline("depthReadFragment");

			Indium::DepthStencilDescriptor depthDescriptor {};
			depthDescriptor.depthCompareFunction = Indium::CompareFunction::Always;
			depthDescriptor.depthWriteEnabled = true;
			auto writeDepth = device->newDepthStencilState(depthDescriptor);
			depthDescriptor.depthWriteEnabled = false;
			auto keepDepth = device->newDepthStencilState(depthDescriptor);

			auto sampler = device->newSamplerState(Indium::SamplerDescriptor {});
			auto colorDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, size, size, false);
			colorDescriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
			auto color = device->newTexture(colorDescriptor);
			auto depthTextureDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::Depth32Float, size, size, false);
			depthTextureDescriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
			auto depth = device->newTexture(depthTextureDescriptor);

			Indium::RenderPassDescriptor pass {};
			pass.colorAttachments.emplace_back();
			pass.colorAttachments[0].texture = color;
			pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
			pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
			pass.colorAttachments[0].clearColor = Indium::ClearColor(0, 0, 0, 0);
			pass.depthAttachment = Indium::RenderPassDepthAttachmentDescriptor();
			pass.depthAttachment->texture = depth;
			pass.depthAttachment->clearDepth = 1.0;
			pass.depthAttachment->loadAction = Indium::LoadAction::Clear;
			pass.depthAttachment->storeAction = Indium::StoreAction::Store;

			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->renderCommandEncoder(pass);
			encoder->setDepthStencilState(writeDepth);
			encoder->setRenderPipelineState(writePipeline);
			encoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);
			encoder->memoryBarrier(Indium::BarrierScope::RenderTargets, Indium::RenderStages::Fragment, Indium::RenderStages::Fragment);
			encoder->setDepthStencilState(keepDepth);
			encoder->setRenderPipelineState(readPipeline);
			encoder->setFragmentTexture(depth, 0);
			encoder->setFragmentSamplerState(sampler, 0);
			encoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();

			std::vector<uint32_t> pixels(size * size);
			color->getBytes(Indium::Region::make2D(0, 0, size, size), 0, pixels.data(), size * 4);
			// the red channel is the depth the first draw wrote, 0.5, so 127 or 128 as a normalized byte
			uint32_t red = pixels[size * size / 2 + size / 2] & 0xFF;
			report(red >= 126 && red <= 129, "the second draw reads the depth the first one wrote");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
