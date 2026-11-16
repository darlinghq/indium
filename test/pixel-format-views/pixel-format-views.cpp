// Copyright (C) 2026 Lubos Dolezel
//
// Pixel formats Vulkan has no direct equivalent of, and views of a texture as another pixel format,
// checked on the real device (lavapipe or a GPU):
//  - a shader reads A8Unorm as (0, 0, 0, a); it reads and writes bytes like any one-byte format (shader writes
//    and render targets are covered by test/a8unorm-write);
//  - X32_Stencil8 / X24_Stencil8 are stencil-only views of Depth32Float_Stencil8 / Depth24Unorm_Stencil8,
//    and of nothing else;
//  - a view of the same texel size (RGBA8Unorm as RGBA8Unorm_sRGB, BGRA8Unorm, R32Uint) needs a texture made
//    with PixelFormatView and decodes sRGB when read; views of another texel size are refused.
// A shader reads a texel with `read` (a fetch, so the conversion and the swizzle are what is observed);
// the stencil and R32Uint views, which that kernel's float type cannot read, are copied out by the blit encoder.
// Prints SKIP and exits 0 without a Vulkan device.

#include "read-float-spv.h"

#include <indium/indium.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t noIndex = SIZE_MAX;

struct Reader {
	std::shared_ptr<Indium::Device> device;
	std::shared_ptr<Indium::CommandQueue> queue;
	std::shared_ptr<Indium::ComputePipelineState> floatPipeline;

	static std::shared_ptr<Indium::ComputePipelineState> pipeline(const std::shared_ptr<Indium::Device>& device, const unsigned char* spirv, size_t length, const char* name) {
		Indium::LibraryReflection reflection;
		auto& function = reflection.functions[name];
		function.functionType = Indium::FunctionType::Kernel;
		function.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
		function.bindings.push_back(Indium::BindingDescriptor { 0, 1, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Sample });
		std::string error;
		auto library = device->newLibrary(spirv, length, reflection, &error);
		if (!library) {
			std::printf("newLibrary: %s\n", error.c_str());
			return nullptr;
		}
		return device->newComputePipelineState(library->newFunction(name));
	}

	// The bytes of the first texel of `texture` (a view's own format and aspect), copied into a buffer by the
	// blit encoder. This reads the data as the view's pixel format names it, with no shader in between.
	std::vector<uint8_t> copyOut(const std::shared_ptr<Indium::Texture>& texture, size_t bytes) {
		auto result = device->newBuffer(16, Indium::ResourceOptions::StorageModeShared);
		std::memset(result->contents(), 0xEE, 16);
		auto commandBuffer = queue->commandBuffer();
		auto encoder = commandBuffer->blitCommandEncoder();
		encoder->copy(texture, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { 1, 1, 1 }, result, 0, 16, 16);
		encoder->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		auto* data = static_cast<const uint8_t*>(result->contents());
		return std::vector<uint8_t>(data, data + bytes);
	}

	bool readFloat(const std::shared_ptr<Indium::Texture>& texture, float out[4]) {
		auto result = device->newBuffer(16, Indium::ResourceOptions::StorageModeShared);
		std::memset(result->contents(), 0xEE, 16);
		auto commandBuffer = queue->commandBuffer();
		auto encoder = commandBuffer->computeCommandEncoder();
		encoder->setComputePipelineState(floatPipeline);
		encoder->setBuffer(result, 0, 0);
		encoder->setTexture(texture, 0);
		encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
		encoder->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		std::memcpy(out, result->contents(), 16);
		return true;
	}
};

bool near(float a, float b) {
	return std::fabs(a - b) < 0.002f;
}

std::shared_ptr<Indium::Texture> newTexture(Indium::Device* device, Indium::PixelFormat format, Indium::TextureUsage usage) {
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(format, 2, 2, false);
	descriptor.usage = usage;
	try {
		return device->newTexture(descriptor);
	} catch (const std::exception& e) {
		std::printf("     newTexture: %s\n", e.what());
		return nullptr;
	}
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

		Reader reader;
		reader.device = Indium::createSystemDefaultDevice();
		std::printf("device: %s\n", reader.device->name().c_str());
		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				reader.device->pollEvents(UINT64_MAX);
			}
		});
		reader.queue = reader.device->newCommandQueue();
		reader.floatPipeline = Reader::pipeline(reader.device, read_float_spv, read_float_spv_len, "readFloat");
		report(reader.floatPipeline != nullptr, "the read kernel loads");

		using Indium::PixelFormat;
		using Indium::TextureUsage;
		auto* device = reader.device.get();

		// --- A8Unorm ---------------------------------------------------------------------------
		{
			auto a8 = newTexture(device, PixelFormat::A8Unorm, TextureUsage::ShaderRead);
			report(a8 != nullptr, "an A8Unorm texture can be made for reading");
			if (a8) {
				const uint8_t bytes[4] = { 0x40, 0x80, 0xC0, 0xFF };
				a8->replaceRegion(Indium::Region::make2D(0, 0, 2, 2), 0, bytes, 2);

				uint8_t back[4] = {};
				a8->getBytes(Indium::Region::make2D(0, 0, 2, 2), 0, back, 2);
				report(std::memcmp(back, bytes, 4) == 0, "A8Unorm bytes read back as written");

				float texel[4];
				reader.readFloat(a8, texel);
				report(texel[0] == 0 && texel[1] == 0 && texel[2] == 0 && near(texel[3], 0x40 / 255.0f), "a shader reads A8Unorm as (0, 0, 0, a)");

				// the same bytes through an R8Unorm view are the red channel instead
				auto asR8 = a8->newTextureView(PixelFormat::R8Unorm);
				report(asR8 == nullptr, "A8Unorm cannot be viewed as R8Unorm without PixelFormatView");

				auto swizzled = a8->newTextureView(PixelFormat::A8Unorm, Indium::TextureType::e2D, Indium::Range<size_t> { 0, 1 }, Indium::Range<size_t> { 0, 1 },
					Indium::TextureSwizzleChannels { Indium::TextureSwizzle::Alpha, Indium::TextureSwizzle::One, Indium::TextureSwizzle::Zero, Indium::TextureSwizzle::Red });
				report(swizzled != nullptr, "an A8Unorm view with a swizzle can be made");
				if (swizzled) {
					reader.readFloat(swizzled, texel);
					// the swizzle names channels of what a shader sees: alpha is a, red is 0
					report(near(texel[0], 0x40 / 255.0f) && texel[1] == 1 && texel[2] == 0 && texel[3] == 0, "a swizzle applies on top of the A8Unorm read");
				}
			}
			// both come with a native A8 format; a device without one refuses both
			report((newTexture(device, PixelFormat::A8Unorm, TextureUsage::ShaderWrite) == nullptr) == (newTexture(device, PixelFormat::A8Unorm, TextureUsage::RenderTarget) == nullptr),
				"an A8Unorm texture can be written by shaders and rendered to, or neither");

			auto r8 = newTexture(device, PixelFormat::R8Unorm, TextureUsage::ShaderRead | TextureUsage::PixelFormatView);
			if (r8) {
				const uint8_t bytes[4] = { 0x40, 0x80, 0xC0, 0xFF };
				r8->replaceRegion(Indium::Region::make2D(0, 0, 2, 2), 0, bytes, 2);
				auto asA8 = r8->newTextureView(PixelFormat::A8Unorm);
				report(asA8 != nullptr, "R8Unorm can be viewed as A8Unorm");
				float texel[4];
				if (asA8) {
					reader.readFloat(asA8, texel);
					report(texel[0] == 0 && near(texel[3], 0x40 / 255.0f), "the red of R8Unorm is the alpha of its A8Unorm view");
				}
				reader.readFloat(r8, texel);
				report(near(texel[0], 0x40 / 255.0f) && texel[1] == 0 && texel[3] == 1, "R8Unorm itself still reads as (r, 0, 0, 1)");
			}
		}

		// --- stencil views ---------------------------------------------------------------------
		{
			auto combined = newTexture(device, PixelFormat::Depth32Float_Stencil8, TextureUsage::RenderTarget | TextureUsage::ShaderRead | TextureUsage::PixelFormatView);
			report(combined != nullptr, "a Depth32Float_Stencil8 texture can be made");
			if (combined) {
				Indium::RenderPassDescriptor pass {};
				pass.depthAttachment = Indium::RenderPassDepthAttachmentDescriptor {};
				pass.depthAttachment->texture = combined;
				pass.depthAttachment->loadAction = Indium::LoadAction::Clear;
				pass.depthAttachment->storeAction = Indium::StoreAction::Store;
				pass.depthAttachment->clearDepth = 0.25;
				pass.stencilAttachment = Indium::RenderPassStencilAttachmentDescriptor {};
				pass.stencilAttachment->texture = combined;
				pass.stencilAttachment->loadAction = Indium::LoadAction::Clear;
				pass.stencilAttachment->storeAction = Indium::StoreAction::Store;
				pass.stencilAttachment->clearStencil = 0x5A;
				auto commandBuffer = reader.queue->commandBuffer();
				auto encoder = commandBuffer->renderCommandEncoder(pass);
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();

				auto stencil = combined->newTextureView(PixelFormat::X32_Stencil8);
				report(stencil != nullptr, "Depth32Float_Stencil8 can be viewed as X32_Stencil8");
				if (stencil) {
					report(stencil->pixelFormat() == PixelFormat::X32_Stencil8, "the stencil view reports its pixel format");
					auto texel = reader.copyOut(stencil, 1);
					report(texel[0] == 0x5A, "copying the X32_Stencil8 view out gives the stencil value");
				}
				float depth[4];
				reader.readFloat(combined, depth);
				report(near(depth[0], 0.25f), "the combined texture itself still reads its depth");

				report(combined->newTextureView(PixelFormat::X24_Stencil8) == nullptr, "Depth32Float_Stencil8 has no X24_Stencil8 view");
				report(combined->newTextureView(PixelFormat::Depth32Float) == nullptr, "Depth32Float_Stencil8 has no Depth32Float view");
				report(combined->newTextureView(PixelFormat::R32Uint) == nullptr, "Depth32Float_Stencil8 has no color view");
			}

			auto depthOnly = newTexture(device, PixelFormat::Depth32Float, TextureUsage::RenderTarget | TextureUsage::ShaderRead | TextureUsage::PixelFormatView);
			report(depthOnly && depthOnly->newTextureView(PixelFormat::X32_Stencil8) == nullptr, "Depth32Float has no stencil view");
			auto stencilOnly = newTexture(device, PixelFormat::Stencil8, TextureUsage::RenderTarget | TextureUsage::ShaderRead | TextureUsage::PixelFormatView);
			report(stencilOnly && stencilOnly->newTextureView(PixelFormat::X32_Stencil8) == nullptr, "Stencil8 has no X32_Stencil8 view");
			report(newTexture(device, PixelFormat::X32_Stencil8, TextureUsage::ShaderRead) == nullptr, "X32_Stencil8 cannot be made as a texture of its own");

			if (device->supportsDepth24Stencil8PixelFormat()) {
				auto combined24 = newTexture(device, PixelFormat::Depth24Unorm_Stencil8, TextureUsage::RenderTarget | TextureUsage::ShaderRead | TextureUsage::PixelFormatView);
				report(combined24 && combined24->newTextureView(PixelFormat::X24_Stencil8) != nullptr, "Depth24Unorm_Stencil8 can be viewed as X24_Stencil8");
			} else {
				std::printf("SKIP Depth24Unorm_Stencil8 is not supported by the device\n");
			}
		}

		// --- same-size views ---------------------------------------------------------------------
		{
			const uint8_t texels[16] = { 128, 64, 255, 200, 128, 64, 255, 200, 128, 64, 255, 200, 128, 64, 255, 200 };

			auto plain = newTexture(device, PixelFormat::RGBA8Unorm, TextureUsage::ShaderRead);
			plain->replaceRegion(Indium::Region::make2D(0, 0, 2, 2), 0, texels, 8);
			report(plain->newTextureView(PixelFormat::RGBA8Unorm_sRGB) == nullptr, "no sRGB view of a texture made without PixelFormatView");
			report(plain->newTextureView(PixelFormat::RGBA8Unorm) != nullptr, "a view of the texture's own format needs no PixelFormatView");

			auto viewable = newTexture(device, PixelFormat::RGBA8Unorm, TextureUsage::ShaderRead | TextureUsage::PixelFormatView);
			viewable->replaceRegion(Indium::Region::make2D(0, 0, 2, 2), 0, texels, 8);

			float linear[4];
			reader.readFloat(viewable, linear);
			report(near(linear[0], 128 / 255.0f) && near(linear[2], 1.0f) && near(linear[3], 200 / 255.0f), "RGBA8Unorm reads the stored values");

			auto srgb = viewable->newTextureView(PixelFormat::RGBA8Unorm_sRGB);
			report(srgb != nullptr, "RGBA8Unorm can be viewed as RGBA8Unorm_sRGB");
			if (srgb) {
				float decoded[4];
				reader.readFloat(srgb, decoded);
				// sRGB decode of 128/255 and 64/255 (IEC 61966-2-1); alpha is not decoded
				report(near(decoded[0], 0.2158605f) && near(decoded[1], 0.0513270f) && near(decoded[2], 1.0f) && near(decoded[3], 200 / 255.0f),
					"the sRGB twin view decodes the color channels and not alpha");
			}

			auto bgra = viewable->newTextureView(PixelFormat::BGRA8Unorm);
			report(bgra != nullptr, "RGBA8Unorm can be viewed as BGRA8Unorm");
			if (bgra) {
				float swapped[4];
				reader.readFloat(bgra, swapped);
				report(near(swapped[0], 1.0f) && near(swapped[2], 128 / 255.0f), "the BGRA8Unorm view swaps red and blue");
			}

			auto raw = viewable->newTextureView(PixelFormat::R32Uint);
			report(raw != nullptr, "RGBA8Unorm can be viewed as R32Uint");
			if (raw) {
				auto bytes = reader.copyOut(raw, 4);
				report(std::memcmp(bytes.data(), texels, 4) == 0, "copying the R32Uint view out gives the four bytes of the texel");
			}

			report(viewable->newTextureView(PixelFormat::R8Unorm) == nullptr, "no view of another texel size (R8Unorm)");
			report(viewable->newTextureView(PixelFormat::RGBA16Float) == nullptr, "no view of another texel size (RGBA16Float)");
			report(viewable->newTextureView(PixelFormat::BC1_RGBA) == nullptr, "no compressed view of an uncompressed texture");
			report(viewable->newTextureView(PixelFormat::BGRA10_XR) == nullptr, "no view of a format with no Vulkan equivalent");
			report(srgb && srgb->newTextureView(PixelFormat::BGRA8Unorm_sRGB) != nullptr, "a view of a view may be viewed as another compatible format");
		}

		keepPollingDevice = false;
		reader.device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
