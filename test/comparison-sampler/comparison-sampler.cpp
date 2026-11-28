// Copyright (C) 2026 Lubos Dolezel
//
// A sampler state's compare function applies only where the shader samples a depth comparison. Metal ignores it for
// an ordinary sample(): an RGBA texture read through a sampler whose compare function is LessEqual (what ANGLE gives
// every sampler, from GL's default TEXTURE_COMPARE_FUNC) returns its texel. Vulkan applies a comparison sampler to
// every read instead, so indium binds one only where the function compares.
//
// comparison-sampler.spv holds two kernels that both take their sampler at binding 2: sample_color reads a colour
// texture with sample(), and sample_depth_compare compares two references against a depth texture with
// sample_compare(). Each runs through samplers with several compare functions. The two sharing a binding number in
// one module checks that what one entry point compares through is not taken for the other's. helper_compare compares
// in a helper function that takes its sampler as a parameter, and with gather_compare, next to an ordinary sample
// through a second sampler, which must still not compare.
// Prints SKIP and exits 0 without a Vulkan device.

#include "comparison-sampler-spv.h"

#include <indium/indium.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	failures += !ok;
}

// what mslc reports for both kernels in comparison-sampler.reflect.json
Indium::FunctionReflection kernelReflection() {
	Indium::FunctionReflection reflection;
	reflection.functionType = Indium::FunctionType::Kernel;
	Indium::BindingDescriptor out;
	out.type = Indium::BindingType::Buffer;
	out.index = 0;
	out.internalIndex = 0;
	Indium::BindingDescriptor texture;
	texture.type = Indium::BindingType::Texture;
	texture.textureAccessType = Indium::TextureAccessType::Sample;
	texture.index = 0;
	texture.internalIndex = 1;
	Indium::BindingDescriptor sampler;
	sampler.type = Indium::BindingType::Sampler;
	sampler.index = 0;
	sampler.internalIndex = 2;
	reflection.bindings = { out, texture, sampler };
	return reflection;
}

// helper_compare: the buffer, the colour and depth textures, the plain and the comparison sampler
Indium::FunctionReflection helperReflection() {
	Indium::FunctionReflection reflection;
	reflection.functionType = Indium::FunctionType::Kernel;
	Indium::BindingDescriptor out;
	out.type = Indium::BindingType::Buffer;
	reflection.bindings.push_back(out);
	for (size_t i = 0; i < 2; ++i) {
		Indium::BindingDescriptor texture;
		texture.type = Indium::BindingType::Texture;
		texture.textureAccessType = Indium::TextureAccessType::Sample;
		texture.index = i;
		texture.internalIndex = 1 + i;
		reflection.bindings.push_back(texture);
	}
	for (size_t i = 0; i < 2; ++i) {
		Indium::BindingDescriptor sampler;
		sampler.type = Indium::BindingType::Sampler;
		sampler.index = i;
		sampler.internalIndex = 3 + i;
		reflection.bindings.push_back(sampler);
	}
	return reflection;
}

const char* compareName(Indium::CompareFunction function) {
	switch (function) {
		case Indium::CompareFunction::Never: return "Never";
		case Indium::CompareFunction::LessEqual: return "LessEqual";
		case Indium::CompareFunction::Greater: return "Greater";
		case Indium::CompareFunction::Always: return "Always";
		default: return "?";
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
		reflection.functions["sample_color"] = kernelReflection();
		reflection.functions["sample_depth_compare"] = kernelReflection();
		reflection.functions["helper_compare"] = helperReflection();
		std::string error;
		auto library = device->newLibrary(comparison_sampler_spv, comparison_sampler_spv_len, reflection, &error);
		report(library != nullptr, "the library loads" + (error.empty() ? std::string() : ": " + error));

		if (library) {
			auto colorPipeline = device->newComputePipelineState(library->newFunction("sample_color"));
			auto depthPipeline = device->newComputePipelineState(library->newFunction("sample_depth_compare"));

			Indium::TextureDescriptor descriptor {};
			descriptor.textureType = Indium::TextureType::e2D;
			descriptor.width = 1;
			descriptor.height = 1;
			descriptor.depth = 1;
			descriptor.arrayLength = 1;
			descriptor.usage = Indium::TextureUsage::ShaderRead;
			descriptor.pixelFormat = Indium::PixelFormat::RGBA8Unorm;
			auto color = device->newTexture(descriptor);
			const uint8_t texel[4] = { 51, 102, 153, 204 };
			color->replaceRegion(Indium::Region { Indium::Origin { 0, 0, 0 }, Indium::Size { 1, 1, 1 } }, 0, 0, texel, 4, 0);
			descriptor.pixelFormat = Indium::PixelFormat::Depth32Float;
			auto depth = device->newTexture(descriptor);
			const float depthTexel = 0.5f;
			depth->replaceRegion(Indium::Region { Indium::Origin { 0, 0, 0 }, Indium::Size { 1, 1, 1 } }, 0, 0, &depthTexel, 4, 0);

			auto run = [&](const std::shared_ptr<Indium::ComputePipelineState>& pipeline, const std::shared_ptr<Indium::Texture>& texture,
					Indium::CompareFunction function, size_t outputBytes) {
				Indium::SamplerDescriptor samplerDescriptor {};
				samplerDescriptor.compareFunction = function;
				auto sampler = device->newSamplerState(samplerDescriptor);
				std::vector<uint8_t> poison(outputBytes, 0xA5);
				auto output = device->newBuffer(poison.data(), poison.size(), Indium::ResourceOptions::StorageModeShared);
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(pipeline);
				encoder->setTexture(texture, 0);
				encoder->setSamplerState(sampler, 0);
				encoder->setBuffer(output, 0, 0);
				encoder->dispatchThreads(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				std::vector<float> result(outputBytes / sizeof(float));
				std::memcpy(result.data(), output->contents(), outputBytes);
				return result;
			};

			for (auto function: { Indium::CompareFunction::Never, Indium::CompareFunction::LessEqual,
					Indium::CompareFunction::Greater, Indium::CompareFunction::Always }) {
				auto result = run(colorPipeline, color, function, 4 * sizeof(float));
				bool ok = true;
				for (int i = 0; i < 4; ++i) {
					ok = ok && std::fabs(result[i] - texel[i] / 255.0f) < 0.002f;
				}
				char got[96];
				std::snprintf(got, sizeof(got), "(%.3f, %.3f, %.3f, %.3f)", result[0], result[1], result[2], result[3]);
				report(ok, std::string("sample() through a ") + compareName(function) + " sampler returns the texel, got " + got);
			}

			// depth 0.5 against the references 0.25 and 0.75: what each compare function makes of them
			struct { Indium::CompareFunction function; float low, high; } comparisons[] = {
				{ Indium::CompareFunction::LessEqual, 1, 0 },
				{ Indium::CompareFunction::Greater, 0, 1 },
				{ Indium::CompareFunction::Always, 1, 1 },
				{ Indium::CompareFunction::Never, 0, 0 },
			};
			for (const auto& c: comparisons) {
				auto result = run(depthPipeline, depth, c.function, 2 * sizeof(float));
				char got[64];
				std::snprintf(got, sizeof(got), "(%.3f, %.3f)", result[0], result[1]);
				report(result[0] == c.low && result[1] == c.high,
					std::string("sample_compare() through a ") + compareName(c.function) + " sampler compares, got " + got);
			}

			auto helperPipeline = device->newComputePipelineState(library->newFunction("helper_compare"));
			for (auto plainFunction: { Indium::CompareFunction::LessEqual, Indium::CompareFunction::Never }) {
				Indium::SamplerDescriptor samplerDescriptor {};
				samplerDescriptor.compareFunction = plainFunction;
				auto plain = device->newSamplerState(samplerDescriptor);
				samplerDescriptor.compareFunction = Indium::CompareFunction::LessEqual;
				auto compare = device->newSamplerState(samplerDescriptor);
				std::vector<float> poison(7, -1.0f);
				auto output = device->newBuffer(poison.data(), poison.size() * sizeof(float), Indium::ResourceOptions::StorageModeShared);
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(helperPipeline);
				encoder->setTexture(color, 0);
				encoder->setTexture(depth, 1);
				encoder->setSamplerState(plain, 0);
				encoder->setSamplerState(compare, 1);
				encoder->setBuffer(output, 0, 0);
				encoder->dispatchThreads(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				const float* result = static_cast<const float*>(output->contents());
				bool texelOk = true;
				for (int i = 0; i < 4; ++i) {
					texelOk = texelOk && std::fabs(result[i] - texel[i] / 255.0f) < 0.002f;
				}
				char got[128];
				std::snprintf(got, sizeof(got), "(%.3f, %.3f, %.3f, %.3f)", result[0], result[1], result[2], result[3]);
				report(texelOk, std::string("helper_compare: sample() through a ") + compareName(plainFunction)
					+ " sampler next to a compared one returns the texel, got " + got);
				std::snprintf(got, sizeof(got), "(%.3f, %.3f, %.3f)", result[4], result[5], result[6]);
				report(result[4] == 1 && result[5] == 0 && result[6] == 1,
					std::string("helper_compare: sample_compare() in a helper and gather_compare() compare, got ") + got);
			}
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
