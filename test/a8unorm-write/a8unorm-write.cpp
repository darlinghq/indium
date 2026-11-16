// Copyright (C) 2026 Lubos Dolezel
//
// A8Unorm textures that a shader writes and that a render pass draws into. The format has only an alpha
// channel, so the alpha a kernel writes or a fragment returns is the one stored byte, whatever the other
// channels hold:
//  - a kernel writing float4(r, g, b, a) to an A8Unorm texture stores a;
//  - a render target of A8Unorm stores the alpha the fragment returns, clears to the alpha of the clear
//    colour, blends with the alpha blend factors and honours the alpha bit of the colour write mask only.
// The shaders are mslc's output for a8.metal in this directory.
// A device that has no native A8 format (no VK_KHR_maintenance5) refuses these usages; the test then only
// checks that the refusal is clean. Prints SKIP and exits 0 without a Vulkan device.

#include "a8-spv.h"

#include <indium/indium.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t width = 8;
constexpr size_t height = 2;
constexpr size_t noIndex = SIZE_MAX;

bool within(uint8_t value, int expected) {
	return std::abs(static_cast<int>(value) - expected) <= 1;
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

		using Indium::PixelFormat;
		using Indium::TextureUsage;
		auto makeTexture = [&](TextureUsage usage) -> std::shared_ptr<Indium::Texture> {
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(PixelFormat::A8Unorm, width, height, false);
			descriptor.usage = usage;
			try {
				return device->newTexture(descriptor);
			} catch (const std::exception&) {
				return nullptr;
			}
		};

		auto writable = makeTexture(TextureUsage::ShaderWrite | TextureUsage::ShaderRead);
		auto target = makeTexture(TextureUsage::RenderTarget | TextureUsage::ShaderRead);
		if (!writable || !target) {
			std::printf("SKIP this device has no native A8Unorm format: shader writes and render targets are refused\n");
			report(!writable && !target, "both A8Unorm usages are refused together");
		} else {
			report(true, "an A8Unorm texture can be made for shader writes and as a render target");
		}

		Indium::LibraryReflection reflection;
		reflection.functions["fullVertex"].functionType = Indium::FunctionType::Vertex;
		reflection.functions["colorFragment"].functionType = Indium::FunctionType::Fragment;
		auto& kernel = reflection.functions["writeTexel"];
		kernel.functionType = Indium::FunctionType::Kernel;
		kernel.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Write });
		std::string error;
		auto library = device->newLibrary(a8_spv, a8_spv_len, reflection, &error);
		report(library != nullptr, "the module loads");
		if (library && writable && target) {
			auto readBack = [&](const std::shared_ptr<Indium::Texture>& texture) {
				std::vector<uint8_t> bytes(width * height, 0xEE);
				texture->getBytes(Indium::Region::make2D(0, 0, width, height), 0, bytes.data(), width);
				return bytes;
			};

			// --- a kernel writes the alpha
			{
				auto pipeline = device->newComputePipelineState(library->newFunction("writeTexel"));
				report(pipeline != nullptr, "the write kernel loads");
				std::vector<uint8_t> zeros(width * height, 0);
				writable->replaceRegion(Indium::Region::make2D(0, 0, width, height), 0, zeros.data(), width);
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(pipeline);
				encoder->setTexture(writable, 0);
				encoder->dispatchThreads(Indium::Size { width, height, 1 }, Indium::Size { 1, 1, 1 });
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				auto bytes = readBack(writable);
				bool all = true;
				for (size_t y = 0; y < height; ++y) {
					for (size_t x = 0; x < width; ++x) {
						// the kernel writes (0.9, 0.8, 0.7, (x + 1) / 8): only the alpha has somewhere to go
						all = all && within(bytes[y * width + x], static_cast<int>((x + 1) * 255 / 8.0 + 0.5));
					}
				}
				report(all, "a kernel's write to an A8Unorm texture stores the alpha, not the red");
			}

			// --- a render pass draws into it
			auto makePipeline = [&](Indium::ColorWriteMask mask, bool blend) {
				Indium::RenderPipelineDescriptor descriptor {};
				descriptor.vertexFunction = library->newFunction("fullVertex");
				descriptor.fragmentFunction = library->newFunction("colorFragment");
				descriptor.colorAttachments.emplace_back();
				auto& attachment = descriptor.colorAttachments[0];
				attachment.pixelFormat = PixelFormat::A8Unorm;
				attachment.writeMask = mask;
				if (blend) {
					attachment.blendingEnabled = true;
					attachment.sourceRGBBlendFactor = Indium::BlendFactor::One;
					attachment.destinationRGBBlendFactor = Indium::BlendFactor::One;
					attachment.sourceAlphaBlendFactor = Indium::BlendFactor::One;
					attachment.destinationAlphaBlendFactor = Indium::BlendFactor::One;
				}
				return device->newRenderPipelineState(descriptor);
			};

			// clears the target to `clearAlpha`, draws the full-target triangle with `pipeline` (the fragment's
			// alpha is 0.5; a null pipeline draws nothing) and returns the bytes
			auto run = [&](float clearAlpha, const std::shared_ptr<Indium::RenderPipelineState>& pipeline) {
				Indium::RenderPassDescriptor pass {};
				pass.colorAttachments.emplace_back();
				pass.colorAttachments[0].texture = target;
				pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
				pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
				// the red, green and blue of the clear colour have no channel to go to
				pass.colorAttachments[0].clearColor = Indium::ClearColor(1, 1, 1, clearAlpha);
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->renderCommandEncoder(pass);
				if (pipeline) {
					encoder->setRenderPipelineState(pipeline);
					encoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);
				}
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				return readBack(target);
			};
			auto allWithin = [](const std::vector<uint8_t>& bytes, int expected) {
				for (auto byte : bytes) {
					if (!within(byte, expected)) {
						return false;
					}
				}
				return true;
			};

			auto plain = makePipeline(Indium::ColorWriteMask::All, false);
			auto blended = makePipeline(Indium::ColorWriteMask::All, true);
			auto noAlpha = makePipeline(Indium::ColorWriteMask::Red | Indium::ColorWriteMask::Green | Indium::ColorWriteMask::Blue, false);
			auto onlyAlpha = makePipeline(Indium::ColorWriteMask::Alpha, false);
			report(plain && blended && noAlpha && onlyAlpha, "pipelines with an A8Unorm attachment can be made");

			report(allWithin(run(0.25f, nullptr), 64), "clearing an A8Unorm render target stores the clear alpha");
			report(allWithin(run(0.25f, plain), 128), "a fragment's alpha is stored in an A8Unorm render target");
			report(allWithin(run(0.25f, blended), 191), "blending adds the alpha of the fragment to the alpha of the target");
			report(allWithin(run(0.25f, noAlpha), 64), "a write mask without alpha leaves an A8Unorm render target alone");
			report(allWithin(run(0.25f, onlyAlpha), 128), "a write mask of alpha alone writes an A8Unorm render target");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
