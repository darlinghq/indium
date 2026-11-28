// Copyright (C) 2026 Lubos Dolezel
//
// Several arguments of a function may take one texture index when function constants leave at most one of them in a
// specialization: ANGLE's blit shaders take their source at texture(0) as a texture2d, a texture2d_ms, a texture3d or
// a texturecube, and a function constant picks one. The module has a binding for each such argument, and indium binds
// the texture at that index only at the binding whose image type the texture's type is: Vulkan does not allow an image
// view at a binding that declares another image type (VUID-vkCmdDraw-viewType-07752, VUID-RuntimeSpirv-samples-08725).
//
// gated-texture-aliases.spv has one kernel, read_source, whose four source arguments at texture(0) are gated by the
// value of the function constant kSourceType. For each value this binds a texture of the matching type and checks that
// only the binding of that argument is given an image view (the others are written null or left unwritten), and that
// the kernel reads the texture's texel.
// Prints SKIP and exits 0 without a Vulkan device.

#include "gated-texture-aliases-spv.h"

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	failures += !ok;
}

// whether each image binding written since the last clear was given a view
std::map<uint32_t, bool> imageWrites;
PFN_vkUpdateDescriptorSets originalUpdate = nullptr;

VKAPI_ATTR void VKAPI_CALL recordUpdate(VkDevice device, uint32_t writeCount, const VkWriteDescriptorSet* writes, uint32_t copyCount, const VkCopyDescriptorSet* copies) {
	for (uint32_t i = 0; i < writeCount; ++i) {
		if (writes[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || writes[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
			imageWrites[writes[i].dstBinding] = writes[i].pImageInfo && writes[i].pImageInfo->imageView != VK_NULL_HANDLE;
		}
	}
	originalUpdate(device, writeCount, writes, copyCount, copies);
}

// the bindings of read_source in gated-texture-aliases.reflect.json
constexpr uint32_t sourceBindings[4] = { 1, 2, 3, 4 };
const char* const sourceNames[4] = { "texture2d", "texture2d_ms", "texture3d", "texturecube" };

Indium::FunctionReflection readSourceReflection() {
	Indium::FunctionReflection reflection;
	reflection.functionType = Indium::FunctionType::Kernel;
	Indium::BindingDescriptor out;
	out.type = Indium::BindingType::Buffer;
	out.index = 0;
	out.internalIndex = 0;
	reflection.bindings.push_back(out);
	const Indium::TextureAccessType access[4] = {
		Indium::TextureAccessType::Sample, Indium::TextureAccessType::Fetch, Indium::TextureAccessType::Sample, Indium::TextureAccessType::Sample,
	};
	for (size_t i = 0; i < 4; ++i) {
		Indium::BindingDescriptor texture;
		texture.type = Indium::BindingType::Texture;
		texture.textureAccessType = access[i];
		texture.index = 0;
		texture.internalIndex = sourceBindings[i];
		reflection.bindings.push_back(texture);
	}
	Indium::BindingDescriptor sampler;
	sampler.type = Indium::BindingType::Sampler;
	sampler.index = 0;
	sampler.internalIndex = 5;
	reflection.bindings.push_back(sampler);

	Indium::FunctionConstantInfo sourceType;
	sourceType.name = "kSourceType";
	sourceType.type = Indium::FunctionConstantType::Int;
	sourceType.index = 0;
	sourceType.specializationID = 65536;
	sourceType.definedSpecializationID = 131072;
	reflection.functionConstants.push_back(sourceType);
	return reflection;
}

std::shared_ptr<Indium::Texture> sourceTexture(const std::shared_ptr<Indium::Device>& device, int type, const uint8_t (&texel)[4]) {
	Indium::TextureDescriptor descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 1, 1, false);
	switch (type) {
		case 0: {
			auto texture = device->newTexture(descriptor);
			texture->replaceRegion(Indium::Region::make2D(0, 0, 1, 1), 0, texel, 4);
			return texture;
		}
		case 1:
			// its samples are not read back: the binding is what is checked
			descriptor.textureType = Indium::TextureType::e2DMultisample;
			descriptor.sampleCount = 4;
			descriptor.usage = Indium::TextureUsage::ShaderRead | Indium::TextureUsage::RenderTarget;
			descriptor.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
			return device->newTexture(descriptor);
		case 2: {
			descriptor.textureType = Indium::TextureType::e3D;
			auto texture = device->newTexture(descriptor);
			texture->replaceRegion(Indium::Region { Indium::Origin { 0, 0, 0 }, Indium::Size { 1, 1, 1 } }, 0, 0, texel, 4, 4);
			return texture;
		}
		default: {
			auto texture = device->newTexture(Indium::TextureDescriptor::textureCubeDescriptor(Indium::PixelFormat::RGBA8Unorm, 1, false));
			// the +X face, which the kernel samples, has the texel and every other face black
			const uint8_t black[4] = { 0, 0, 0, 255 };
			for (size_t face = 0; face < 6; ++face) {
				texture->replaceRegion(Indium::Region::make2D(0, 0, 1, 1), 0, face, face == 0 ? texel : black, 4, 4);
			}
			return texture;
		}
	}
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
		if (!Indium::DynamicVK::vkUpdateDescriptorSets.resolve()) {
			return 3;
		}

		auto device = Indium::createSystemDefaultDevice();
		std::printf("device: %s\n", device->name().c_str());
		auto queue = device->newCommandQueue();
		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		Indium::LibraryReflection reflection;
		reflection.functions["read_source"] = readSourceReflection();
		std::string error;
		auto library = device->newLibrary(gated_texture_aliases_spv, gated_texture_aliases_spv_len, reflection, &error);
		report(library != nullptr, "the library loads" + (error.empty() ? std::string() : ": " + error));

		if (library) {
			auto sampler = device->newSamplerState(Indium::SamplerDescriptor {});
			auto out = device->newBuffer(4 * sizeof(float), Indium::ResourceOptions::StorageModeShared);
			const uint8_t texel[4] = { 51, 102, 153, 204 };

			originalUpdate = reinterpret_cast<PFN_vkUpdateDescriptorSets>(Indium::DynamicVK::vkUpdateDescriptorSets.pointer);
			Indium::DynamicVK::vkUpdateDescriptorSets.pointer = reinterpret_cast<void*>(recordUpdate);

			for (int type = 0; type < 4; ++type) {
				Indium::FunctionConstantValues values;
				values.setValue(0, Indium::FunctionConstantType::Int, static_cast<uint32_t>(type));
				std::string message;
				auto function = library->newFunction("read_source", values, &message);
				if (!function) {
					report(false, std::string("read_source with the ") + sourceNames[type] + " source specializes: " + message);
					continue;
				}
				auto pipeline = device->newComputePipelineState(function);
				auto texture = sourceTexture(device, type, texel);
				std::memset(out->contents(), 0, 4 * sizeof(float));
				imageWrites.clear();

				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(pipeline);
				encoder->setBuffer(out, 0, 0);
				encoder->setTexture(texture, 0);
				encoder->setSamplerState(sampler, 0);
				encoder->dispatchThreads(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();

				std::string viewed;
				bool onlyLive = true;
				for (int other = 0; other < 4; ++other) {
					auto written = imageWrites.find(sourceBindings[other]);
					bool hasView = written != imageWrites.end() && written->second;
					if (hasView) {
						viewed += std::string(viewed.empty() ? "" : ", ") + sourceNames[other];
					}
					onlyLive = onlyLive && hasView == (other == type);
				}
				report(onlyLive, std::string("a ") + sourceNames[type] + " texture at texture(0) is bound only at the " + sourceNames[type]
					+ " argument's binding, got a view at: " + (viewed.empty() ? "none" : viewed));

				if (type != 1) {
					const float* result = static_cast<const float*>(out->contents());
					bool readsTexel = true;
					for (int c = 0; c < 4; ++c) {
						float expected = texel[c] / 255.0f;
						readsTexel = readsTexel && result[c] > expected - 0.01f && result[c] < expected + 0.01f;
					}
					char got[96];
					std::snprintf(got, sizeof(got), "(%.3f, %.3f, %.3f, %.3f)", result[0], result[1], result[2], result[3]);
					report(readsTexel, std::string("read_source reads the texel of the ") + sourceNames[type] + " source, got " + got);
				}
			}

			Indium::DynamicVK::vkUpdateDescriptorSets.pointer = reinterpret_cast<void*>(originalUpdate);
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures == 0 ? 0 : 1;
}
