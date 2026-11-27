// Copyright (C) 2026 Lubos Dolezel
//
// The extended-range pixel formats BGRA10_XR, BGRA10_XR_sRGB, BGR10_XR and BGR10_XR_sRGB: what a texture of
// these formats stores and returns. The expected numbers are the documented encoding (see gen-fixtures.py).
//
//  - bytes put in with replaceRegion come back from getBytes unchanged (every code of every channel),
//  - a shader sees the value (code - 384) / 510, through the sRGB curve in the sRGB formats, and alpha clamped,
//  - linear filtering of the sRGB formats happens on the values after the curve,
//  - what a render pass writes (a clear color, shader output, blending) is stored as the nearest code, within
//    the range of the format, which getBytes shows,
//  - the things a texture of these formats cannot do are refused.

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <indium/texture.private.hpp>

#include <array>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <string>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "fixtures.hpp"
#include "vertex-shader.h"
#include "fill-shader.h"
#include "blend-shader.h"
#include "sample_nearest-shader.h"
#include "sample_halfway-shader.h"

using Indium::PixelFormat;

static unsigned gFailures = 0;
static unsigned gChecks = 0;

static void expect(bool condition, const std::string& what, const std::string& detail = "") {
	++gChecks;
	std::printf("%s %s%s%s\n", condition ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
	if (!condition) {
		++gFailures;
	}
}

struct FormatInfo {
	PixelFormat format;
	const char* name;
	bool alpha;
	bool srgb;
	size_t bytes;
};

static const FormatInfo formats[] = {
	{ PixelFormat::BGRA10_XR, "BGRA10_XR", true, false, 8 },
	{ PixelFormat::BGRA10_XR_sRGB, "BGRA10_XR_sRGB", true, true, 8 },
	{ PixelFormat::BGR10_XR, "BGR10_XR", false, false, 4 },
	{ PixelFormat::BGR10_XR_sRGB, "BGR10_XR_sRGB", false, true, 4 },
};

// the bytes of a texel with these codes (blue, green, red, alpha), laid out as the documentation says
static void putTexel(const FormatInfo& info, uint8_t* destination, uint32_t blue, uint32_t green, uint32_t red, uint32_t alpha) {
	if (info.alpha) {
		uint16_t chunks[4] = { uint16_t(blue << 6), uint16_t(green << 6), uint16_t(red << 6), uint16_t(alpha << 6) };
		std::memcpy(destination, chunks, 8);
	} else {
		uint32_t word = blue | (green << 10) | (red << 20);
		std::memcpy(destination, &word, 4);
	}
}

static uint32_t floatBits(float value) {
	uint32_t bits;
	std::memcpy(&bits, &value, 4);
	return bits;
}

// equal to the expected bits, or for the sRGB formats (whose curve a GPU computes in its own way) within a few ulp
static bool closeTo(float got, uint32_t expectedBits, bool tolerant) {
	if (floatBits(got) == expectedBits) {
		return true;
	}
	if (!tolerant) {
		return false;
	}
	float expected;
	std::memcpy(&expected, &expectedBits, 4);
	return std::fabs(got - expected) <= 4e-6f * std::fmax(1.0f, std::fabs(expected));
}

struct Context {
	std::shared_ptr<Indium::Device> device;
	std::shared_ptr<Indium::CommandQueue> queue;
	std::shared_ptr<Indium::ComputePipelineState> nearest, halfway;
	std::shared_ptr<Indium::SamplerState> nearestSampler, linearSampler;
};

static std::shared_ptr<Indium::Texture> makeTexture(Context& context, PixelFormat format, size_t width, size_t height, Indium::TextureUsage usage) {
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(format, width, height, false);
	descriptor.resourceOptions = Indium::ResourceOptions::StorageModeShared;
	descriptor.usage = usage;
	return context.device->newTexture(descriptor);
}

// samples `source` for every texel of a width x 1 result, with the kernel and the sampler, and returns RGBA floats
static std::vector<float> sample(Context& context, const std::shared_ptr<Indium::Texture>& source, size_t width, bool halfway) {
	auto result = makeTexture(context, PixelFormat::RGBA32Float, width, 1, Indium::TextureUsage::ShaderWrite | Indium::TextureUsage::ShaderRead);
	auto commandBuffer = context.queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(halfway ? context.halfway : context.nearest);
	encoder->setTexture(source, 0);
	encoder->setSamplerState(halfway ? context.linearSampler : context.nearestSampler, 0);
	encoder->setTexture(result, 1);
	encoder->dispatchThreads(Indium::Size { width, 1, 1 }, Indium::Size { 1, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	std::vector<float> values(width * 4, -77.0f);
	result->getBytes(Indium::Region::make2D(0, 0, width, 1), 0, values.data(), width * 16);
	return values;
}

static void testRawBytesRoundTrip(Context& context, const FormatInfo& info) {
	const size_t count = 1024;
	auto texture = makeTexture(context, info.format, count, 1, Indium::TextureUsage::ShaderRead);
	std::vector<uint8_t> bytes(count * info.bytes), back(count * info.bytes, 0xaa);
	for (size_t i = 0; i < count; ++i) {
		putTexel(info, &bytes[i * info.bytes], i, (i + 1) % 1024, (i + 2) % 1024, 384 + i % 511);
	}
	texture->replaceRegion(Indium::Region::make2D(0, 0, count, 1), 0, bytes.data(), bytes.size());
	texture->getBytes(Indium::Region::make2D(0, 0, count, 1), 0, back.data(), back.size());
	size_t bad = 0;
	for (size_t i = 0; i < bytes.size(); ++i) {
		bad += bytes[i] != back[i];
	}
	expect(bad == 0, std::string(info.name) + ": every code of every channel survives replaceRegion and getBytes", std::to_string(bad) + " bytes differ");

	// a sub-region with a padded row pitch
	std::vector<uint8_t> sub(3 * 2 * info.bytes + 2 * 5, 0xaa);
	texture->getBytes(Indium::Region::make2D(10, 0, 3, 1), 0, sub.data(), 3 * info.bytes);
	expect(std::memcmp(sub.data(), &bytes[10 * info.bytes], 3 * info.bytes) == 0, std::string(info.name) + ": a sub-region reads back its own texels");

	// padding bits that are not zero are not part of the value, and are zero when read back
	std::vector<uint8_t> dirty(info.bytes), clean(info.bytes), readback(info.bytes);
	putTexel(info, clean.data(), 500, 600, 700, 600);
	dirty = clean;
	if (info.alpha) {
		dirty[0] |= 0x3f;
		dirty[2] |= 0x3f;
		dirty[4] |= 0x3f;
		dirty[6] |= 0x3f;
	} else {
		dirty[3] |= 0xc0;
	}
	texture->replaceRegion(Indium::Region::make2D(0, 0, 1, 1), 0, dirty.data(), info.bytes);
	texture->getBytes(Indium::Region::make2D(0, 0, 1, 1), 0, readback.data(), info.bytes);
	expect(readback == clean, std::string(info.name) + ": padding bits are not stored");

	// QUIRK of the formats with alpha: an alpha code outside 384 to 894 reads clamped to 0 or 1, and so comes back as 384 or 894
	if (info.alpha) {
		std::vector<uint8_t> low(8), high(8), expectedLow(8), expectedHigh(8), gotLow(8), gotHigh(8);
		putTexel(info, low.data(), 500, 500, 500, 0);
		putTexel(info, high.data(), 500, 500, 500, 1023);
		putTexel(info, expectedLow.data(), 500, 500, 500, 384);
		putTexel(info, expectedHigh.data(), 500, 500, 500, 894);
		texture->replaceRegion(Indium::Region::make2D(0, 0, 1, 1), 0, low.data(), 8);
		texture->getBytes(Indium::Region::make2D(0, 0, 1, 1), 0, gotLow.data(), 8);
		texture->replaceRegion(Indium::Region::make2D(1, 0, 1, 1), 0, high.data(), 8);
		texture->getBytes(Indium::Region::make2D(1, 0, 1, 1), 0, gotHigh.data(), 8);
		expect(gotLow == expectedLow && gotHigh == expectedHigh, std::string(info.name) + ": an alpha code outside the range of 0 to 1 reads back clamped");
	}
}

static void testSampledValues(Context& context, const FormatInfo& info) {
	const size_t count = sizeof(codeFixtures) / sizeof(codeFixtures[0]);
	auto texture = makeTexture(context, info.format, count, 1, Indium::TextureUsage::ShaderRead);
	std::vector<uint8_t> bytes(count * info.bytes);
	for (size_t i = 0; i < count; ++i) {
		uint32_t code = codeFixtures[i].code;
		// alpha takes a code of its own range, so every texel of the color check has an in-range one as well
		putTexel(info, &bytes[i * info.bytes], code, code, code, code);
	}
	texture->replaceRegion(Indium::Region::make2D(0, 0, count, 1), 0, bytes.data(), bytes.size());
	auto values = sample(context, texture, count, false);
	size_t badColor = 0, badAlpha = 0;
	for (size_t i = 0; i < count; ++i) {
		uint32_t colorBits = info.srgb ? codeFixtures[i].srgbBits : codeFixtures[i].linearBits;
		for (int channel = 0; channel < 3; ++channel) {
			badColor += !closeTo(values[i * 4 + channel], colorBits, info.srgb);
		}
		float alpha = values[i * 4 + 3];
		badAlpha += info.alpha ? !closeTo(alpha, codeFixtures[i].alphaBits, false) : alpha != 1.0f;
		if (badColor + badAlpha != 0 && i + 1 == count) {
			std::printf("  last code %u: got %g %g %g %g\n", codeFixtures[i].code, values[i * 4], values[i * 4 + 1], values[i * 4 + 2], alpha);
		}
	}
	expect(badColor == 0, std::string(info.name) + ": sampling returns (code - 384) / 510" + (info.srgb ? " through the sRGB curve" : ""), std::to_string(badColor) + " channels differ");
	expect(badAlpha == 0, std::string(info.name) + (info.alpha ? ": alpha is the code's value clamped to 0 to 1" : ": alpha reads as 1"), std::to_string(badAlpha) + " differ");
}

static void testLinearFiltering(Context& context, const FormatInfo& info) {
	auto texture = makeTexture(context, info.format, 2, 1, Indium::TextureUsage::ShaderRead);
	std::vector<uint8_t> bytes(2 * info.bytes);
	putTexel(info, &bytes[0], filterCodeA, filterCodeA, filterCodeA, filterCodeA);
	putTexel(info, &bytes[info.bytes], filterCodeB, filterCodeB, filterCodeB, filterCodeB);
	texture->replaceRegion(Indium::Region::make2D(0, 0, 2, 1), 0, bytes.data(), bytes.size());
	auto values = sample(context, texture, 1, true);
	uint32_t expected = info.srgb ? filterSrgbBits : filterLinearBits;
	expect(closeTo(values[0], expected, true) && closeTo(values[1], expected, true) && closeTo(values[2], expected, true),
		std::string(info.name) + ": halfway between two texels is the mean of their values", "got " + std::to_string(values[0]));
}

// the bytes a render pass left in a width x 1 texture, as codes (blue, green, red, alpha) of each texel
static std::vector<std::array<uint32_t, 4>> readCodes(const FormatInfo& info, const std::shared_ptr<Indium::Texture>& texture, size_t width) {
	std::vector<uint8_t> bytes(width * info.bytes, 0xaa);
	texture->getBytes(Indium::Region::make2D(0, 0, width, 1), 0, bytes.data(), bytes.size());
	std::vector<std::array<uint32_t, 4>> codes(width);
	for (size_t i = 0; i < width; ++i) {
		if (info.alpha) {
			uint16_t chunks[4];
			std::memcpy(chunks, &bytes[i * 8], 8);
			codes[i] = { uint32_t(chunks[0] >> 6), uint32_t(chunks[1] >> 6), uint32_t(chunks[2] >> 6), uint32_t(chunks[3] >> 6) };
			for (uint16_t chunk: chunks) {
				if (chunk & 0x3f) {
					codes[i][0] = 0xffff;
				}
			}
		} else {
			uint32_t word;
			std::memcpy(&word, &bytes[i * 4], 4);
			codes[i] = { word & 0x3ff, (word >> 10) & 0x3ff, (word >> 20) & 0x3ff, 0 };
			if (word >> 30) {
				codes[i][0] = 0xffff;
			}
		}
	}
	return codes;
}

static std::shared_ptr<Indium::RenderPipelineState> makePipeline(Context& context, PixelFormat format, const unsigned char* fragment, unsigned fragmentLength, bool blend) {
	Indium::LibraryReflection vertexReflection, fragmentReflection;
	vertexReflection.functions["main"].functionType = Indium::FunctionType::Vertex;
	fragmentReflection.functions["main"].functionType = Indium::FunctionType::Fragment;
	auto vertexLibrary = context.device->newLibrary(vertex_shader, vertex_shader_len, vertexReflection);
	auto fragmentLibrary = context.device->newLibrary(fragment, fragmentLength, fragmentReflection);
	Indium::RenderPipelineDescriptor descriptor {};
	descriptor.vertexFunction = vertexLibrary->newFunction("main");
	descriptor.fragmentFunction = fragmentLibrary->newFunction("main");
	Indium::RenderPipelineColorAttachmentDescriptor color {};
	color.pixelFormat = format;
	if (blend) {
		color.blendingEnabled = true;
		color.sourceRGBBlendFactor = Indium::BlendFactor::SourceAlpha;
		color.destinationRGBBlendFactor = Indium::BlendFactor::OneMinusSourceAlpha;
		color.sourceAlphaBlendFactor = Indium::BlendFactor::SourceAlpha;
		color.destinationAlphaBlendFactor = Indium::BlendFactor::OneMinusSourceAlpha;
	}
	descriptor.colorAttachments.push_back(color);
	return context.device->newRenderPipelineState(descriptor);
}

static void render(Context& context, const std::shared_ptr<Indium::Texture>& target, Indium::ClearColor clear, const std::shared_ptr<Indium::RenderPipelineState>& pipeline) {
	Indium::RenderPassDescriptor pass {};
	Indium::RenderPassColorAttachmentDescriptor attachment {};
	attachment.texture = target;
	attachment.loadAction = Indium::LoadAction::Clear;
	attachment.storeAction = Indium::StoreAction::Store;
	attachment.clearColor = clear;
	pass.colorAttachments.push_back(attachment);
	auto commandBuffer = context.queue->commandBuffer();
	auto encoder = commandBuffer->renderCommandEncoder(pass);
	if (pipeline) {
		encoder->setRenderPipelineState(pipeline);
		encoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);
	}
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
}

static void testRendering(Context& context, const FormatInfo& info) {
	const size_t count = sizeof(writeFixtures) / sizeof(writeFixtures[0]);
	auto usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;

	// shader output of values inside and outside the range
	auto texture = makeTexture(context, info.format, count, 1, usage);
	render(context, texture, Indium::ClearColor(0, 0, 0, 0), makePipeline(context, info.format, fill_shader, fill_shader_len, false));
	auto codes = readCodes(info, texture, count);
	size_t badColor = 0, badAlpha = 0;
	for (size_t i = 0; i < count; ++i) {
		uint32_t expected = info.srgb ? writeFixtures[i].srgbCode : writeFixtures[i].linearCode;
		for (int channel = 0; channel < 3; ++channel) {
			if (codes[i][channel] != expected) {
				if (badColor++ < 3) {
					std::printf("  value bits %08x channel %d: got code %u, want %u\n", writeFixtures[i].valueBits, channel, codes[i][channel], expected);
				}
			}
		}
		if (info.alpha && codes[i][3] != writeFixtures[i].alphaCode) {
			if (badAlpha++ < 3) {
				std::printf("  value bits %08x alpha: got code %u, want %u\n", writeFixtures[i].valueBits, codes[i][3], writeFixtures[i].alphaCode);
			}
		}
	}
	expect(badColor == 0, std::string(info.name) + ": shader output is stored as the nearest code within the range", std::to_string(badColor) + " channels differ");
	expect(badAlpha == 0, std::string(info.name) + (info.alpha ? ": alpha output is stored as the nearest code of 0 to 1" : ": there is no alpha to store"));

	// what is stored is what a shader samples next
	auto values = sample(context, texture, count, false);
	size_t badSampled = 0;
	for (size_t i = 0; i < count; ++i) {
		uint32_t expected = info.srgb ? writeFixtures[i].srgbCode : writeFixtures[i].linearCode;
		// the value of the code, derived from the fixture table of codes through the same formula
		float linearValue = (float(expected) - 384.0f) / 510.0f;
		float expectedValue = linearValue;
		if (info.srgb) {
			float magnitude = std::fabs(linearValue);
			float decoded = magnitude <= 0.04045f ? magnitude / 12.92f : std::pow((magnitude + 0.055f) / 1.055f, 2.4f);
			expectedValue = std::copysign(decoded, linearValue);
		}
		if (std::fabs(values[i * 4] - expectedValue) > 4e-6f * std::fmax(1.0f, std::fabs(expectedValue))) {
			++badSampled;
		}
	}
	expect(badSampled == 0, std::string(info.name) + ": a texture rendered to samples as the rounded values", std::to_string(badSampled) + " differ");

	// a clear color outside the range
	auto cleared = makeTexture(context, info.format, 2, 1, usage);
	render(context, cleared, Indium::ClearColor(1.5, -1.0, 0.5, 2.0), nullptr);
	auto clearedCodes = readCodes(info, cleared, 2);
	const uint16_t* want = info.srgb ? clearSrgbCodes : clearLinearCodes;
	bool clearOk = clearedCodes[1][2] == want[0] && clearedCodes[1][1] == want[1] && clearedCodes[1][0] == want[2] && (!info.alpha || clearedCodes[1][3] == want[3]);
	expect(clearOk, std::string(info.name) + ": a clear color is rounded and clamped to the range",
		"red " + std::to_string(clearedCodes[1][2]) + " green " + std::to_string(clearedCodes[1][1]) + " blue " + std::to_string(clearedCodes[1][0]) + " alpha " + std::to_string(clearedCodes[1][3]));

	// blending on the values, rounded afterwards
	auto blended = makeTexture(context, info.format, 2, 1, usage);
	render(context, blended, Indium::ClearColor(0.25, 0.5, 0.75, 1.0), makePipeline(context, info.format, blend_shader, blend_shader_len, true));
	auto blendedCodes = readCodes(info, blended, 2);
	const uint16_t* blendWant = info.srgb ? blendSrgbCodes : blendLinearCodes;
	bool blendOk = blendedCodes[0][2] == blendWant[0] && blendedCodes[0][1] == blendWant[1] && blendedCodes[0][0] == blendWant[2] && (!info.alpha || blendedCodes[0][3] == blendAlphaCode);
	expect(blendOk, std::string(info.name) + ": blending happens on the values and the result is rounded to codes",
		"red " + std::to_string(blendedCodes[0][2]) + " green " + std::to_string(blendedCodes[0][1]) + " blue " + std::to_string(blendedCodes[0][0]));
}


// The kernel that converts the gamma encoded values of a non-sRGB format to linear light for presentation, run on
// an RGBA32Float texture: red, green and blue go through the sRGB curve (mirrored below zero), alpha is left alone.
static void testEncodedToLinearKernel(Context& context) {
	namespace DynamicVK = Indium::DynamicVK;
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(context.device);
	const float inputs[] = { -0.752941f, -0.3f, -0.03f, 0.0f, 0.03f, 0.3f, 0.5f, 1.0f, 1.25098f, 2.0f };
	const size_t count = sizeof(inputs) / sizeof(inputs[0]);
	auto texture = makeTexture(context, PixelFormat::RGBA32Float, count, 1, Indium::TextureUsage::ShaderRead | Indium::TextureUsage::ShaderWrite);
	std::vector<float> texels(count * 4);
	for (size_t i = 0; i < count; ++i) {
		texels[i * 4] = inputs[i];
		texels[i * 4 + 1] = inputs[count - 1 - i];
		texels[i * 4 + 2] = inputs[i];
		texels[i * 4 + 3] = 0.25f + 0.1f * i;
	}
	texture->replaceRegion(Indium::Region::make2D(0, 0, count, 1), 0, texels.data(), count * 16);
	privateDevice->textureUploader().flush();

	VkDevice device = privateDevice->device();
	auto& kernel = privateDevice->extendedRangeKernel();
	VkDescriptorPoolSize poolSize { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 };
	VkDescriptorPoolCreateInfo poolInfo {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	DynamicVK::vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool);
	VkDescriptorSetLayout layout = kernel.setLayout();
	VkDescriptorSetAllocateInfo setInfo {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	setInfo.descriptorPool = pool;
	setInfo.descriptorSetCount = 1;
	setInfo.pSetLayouts = &layout;
	VkDescriptorSet set = VK_NULL_HANDLE;
	DynamicVK::vkAllocateDescriptorSets(device, &setInfo, &set);

	VkCommandPoolCreateInfo commandPoolInfo {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.queueFamilyIndex = *privateDevice->graphicsQueueFamilyIndex();
	VkCommandPool commandPool = VK_NULL_HANDLE;
	DynamicVK::vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool);
	VkCommandBufferAllocateInfo allocateInfo {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.commandPool = commandPool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	DynamicVK::vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer);
	VkCommandBufferBeginInfo beginInfo {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	DynamicVK::vkBeginCommandBuffer(commandBuffer, &beginInfo);
	auto privateTexture = std::dynamic_pointer_cast<Indium::PrivateTexture>(texture);
	kernel.record(commandBuffer, set, privateTexture->imageView(), count, 1, Indium::ExtendedRangeKernel::Mode::EncodedToLinear);
	DynamicVK::vkEndCommandBuffer(commandBuffer);

	VkFenceCreateInfo fenceInfo {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence = VK_NULL_HANDLE;
	DynamicVK::vkCreateFence(device, &fenceInfo, nullptr, &fence);
	VkSubmitInfo submit {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &commandBuffer;
	{
		std::lock_guard lock(privateDevice->graphicsQueueMutex());
		DynamicVK::vkQueueSubmit(privateDevice->graphicsQueue(), 1, &submit, fence);
	}
	DynamicVK::vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
	DynamicVK::vkDestroyFence(device, fence, nullptr);
	DynamicVK::vkDestroyCommandPool(device, commandPool, nullptr);
	DynamicVK::vkDestroyDescriptorPool(device, pool, nullptr);

	std::vector<float> values(count * 4);
	texture->getBytes(Indium::Region::make2D(0, 0, count, 1), 0, values.data(), count * 16);
	auto curve = [](float encoded) {
		float magnitude = std::fabs(encoded);
		float linear = magnitude <= 0.04045f ? magnitude / 12.92f : std::pow((magnitude + 0.055f) / 1.055f, 2.4f);
		return std::copysign(linear, encoded);
	};
	size_t bad = 0;
	for (size_t i = 0; i < count; ++i) {
		float want[3] = { curve(texels[i * 4]), curve(texels[i * 4 + 1]), curve(texels[i * 4 + 2]) };
		for (int channel = 0; channel < 3; ++channel) {
			bad += std::fabs(values[i * 4 + channel] - want[channel]) > 4e-6f * std::fmax(1.0f, std::fabs(want[channel]));
		}
		bad += values[i * 4 + 3] != texels[i * 4 + 3];
	}
	expect(bad == 0, "the presentation pass turns gamma encoded values into linear light and leaves alpha alone", std::to_string(bad) + " differ");
}

static void testRefusals(Context& context, const FormatInfo& info) {
	auto texture = makeTexture(context, info.format, 4, 4, Indium::TextureUsage::ShaderRead | Indium::TextureUsage::PixelFormatView);
	expect(texture != nullptr, std::string(info.name) + ": a texture can be made");
	expect(texture && texture->newTextureView(PixelFormat::RGBA8Unorm) == nullptr, std::string(info.name) + ": no view of another format");
	expect(texture && texture->newTextureView(PixelFormat::RGBA32Float) == nullptr, std::string(info.name) + ": no view as the float format of its image");
	auto ordinary = makeTexture(context, PixelFormat::RGBA16Unorm, 4, 4, Indium::TextureUsage::ShaderRead | Indium::TextureUsage::PixelFormatView);
	expect(ordinary && ordinary->newTextureView(info.format) == nullptr, std::string(info.name) + ": no view of a texture of another format");

	bool refused = false;
	try {
		auto buffer = context.device->newBuffer(4 * 4 * 8, Indium::ResourceOptions::StorageModeShared);
		auto commandBuffer = context.queue->commandBuffer();
		auto encoder = commandBuffer->blitCommandEncoder();
		encoder->copy(buffer, 0, 4 * info.bytes, 0, Indium::Size { 4, 4, 1 }, texture, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::BlitOption::None);
		encoder->endEncoding();
	} catch (const std::exception&) {
		refused = true;
	}
	expect(refused, std::string(info.name) + ": a copy from a buffer is refused rather than copying the wrong bytes");
}

int main() {
	Indium::init(nullptr, 0, false);
	Context context;
	context.device = Indium::createSystemDefaultDevice();
	if (!context.device) {
		std::fprintf(stderr, "no Vulkan device\n");
		return 2;
	}
	std::atomic<bool> running { true };
	std::thread polling([&] { while (running) context.device->pollEvents(UINT64_MAX); });
	try {
		context.queue = context.device->newCommandQueue();

		auto makeKernel = [&](const unsigned char* shader, unsigned length) {
			Indium::LibraryReflection reflection;
			auto& kernel = reflection.functions["main"];
			kernel.functionType = Indium::FunctionType::Kernel;
			constexpr size_t noIndex = std::numeric_limits<size_t>::max();
			kernel.bindings.push_back(Indium::BindingDescriptor { 0, 1, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Sample });
			kernel.bindings.push_back(Indium::BindingDescriptor { 0, 2, noIndex, Indium::BindingType::Sampler, Indium::TextureAccessType::Sample });
			kernel.bindings.push_back(Indium::BindingDescriptor { 1, 3, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Write });
			std::string error;
			auto library = context.device->newLibrary(shader, length, reflection, &error);
			if (!library) {
				throw std::runtime_error("kernel library: " + error);
			}
			return context.device->newComputePipelineState(library->newFunction("main"));
		};
		context.nearest = makeKernel(sample_nearest_shader, sample_nearest_shader_len);
		context.halfway = makeKernel(sample_halfway_shader, sample_halfway_shader_len);

		Indium::SamplerDescriptor samplerDescriptor;
		context.nearestSampler = context.device->newSamplerState(samplerDescriptor);
		samplerDescriptor.minFilter = samplerDescriptor.magFilter = Indium::SamplerMinMagFilter::Linear;
		context.linearSampler = context.device->newSamplerState(samplerDescriptor);

		testEncodedToLinearKernel(context);
		for (const auto& info: formats) {
			testRefusals(context, info);
			testRawBytesRoundTrip(context, info);
			testSampledValues(context, info);
			testLinearFiltering(context, info);
			testRendering(context, info);
		}
	} catch (const std::exception& error) {
		std::printf("FAIL exception: %s\n", error.what());
		++gFailures;
	}
	running = false;
	context.device->wakeupEventLoop();
	polling.join();
	context = Context {};
	Indium::finit();
	std::printf("RESULT checks=%u failures=%u\n", gChecks, gFailures);
	return gFailures ? 1 : 0;
}
