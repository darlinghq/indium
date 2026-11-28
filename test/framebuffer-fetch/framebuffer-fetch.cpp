// Copyright (C) 2026 Darling Developers
//
// Framebuffer fetch: a fragment function that reads a color attachment of its render pass (`[[color(n)]]` in the
// Metal Shading Language, a SubpassData image read with OpImageRead in SPIR-V) sees the value that attachment holds
// at the fragment's position: what the pass cleared or loaded it to, then what the earlier draws wrote.
// The reflection says BindingType::InputAttachment with the color attachment index and the descriptor binding.
//
// Drawn on the device, and read back:
//  - one function written by hand in SPIR-V (OpImageRead of a SubpassData image), and the functions of
//    framebuffer-fetch.metal as mslc compiles them (README.md has the commands);
//  - the value of the clear, of a loaded texture, of an earlier draw of the same pass (two inversions return the
//    start; a constant draw followed by an inversion), at attachment 1 of two, with attachment 0 empty, and of an
//    integer attachment;
//  - the state set before the pipeline (a scissor rectangle) still applies after it.
// And what it costs, by the Vulkan calls the library makes:
//  - only render targets that a function can read, single-sample color ones, are made with the input attachment
//    usage (not shader-only, multisample or depth textures);
//  - a render pass that no pipeline of the fetch kind is set in has no input attachment references and its
//    self-dependency has no input attachment access; one that is gets them.
// And that every pipeline bound is one made for a render pass of the class of the pass it is bound in (with or
// without input attachment references: render pass compatibility counts them): plain and fetching draws in
// either order in one pass, and in two.
// And the refusals: reading an attachment the pass does not have, or a multisample one, and reflection that cannot
// be a fragment function's input attachment. Prints SKIP and exits 0 without a Vulkan device.

#include "fetch-spv.h"
#include "handwritten-spv.h"

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	std::fflush(stdout);
	failures += !ok;
}

constexpr size_t size = 8;

// ---- what the library asks Vulkan for ------------------------------------------------------------------

struct PassRecord {
	uint32_t inputAttachments;
	bool selfDependencyReadsInput;
};
std::vector<PassRecord> passRecords;
std::vector<VkImageUsageFlags> imageUsages;
PFN_vkCreateRenderPass originalCreateRenderPass = nullptr;
PFN_vkCreateImage originalCreateImage = nullptr;
PFN_vkCreateGraphicsPipelines originalCreateGraphicsPipelines = nullptr;
PFN_vkCmdBindPipeline originalCmdBindPipeline = nullptr;
PFN_vkCmdBeginRenderPass originalCmdBeginRenderPass = nullptr;

// the class of a render pass is whether it has input attachment references
std::map<VkRenderPass, bool> passHasInput;
std::map<VkPipeline, bool> pipelineMadeForInput;
bool currentPassHasInput = false;
unsigned graphicsBinds = 0;
unsigned classMismatches = 0;

VKAPI_ATTR VkResult VKAPI_CALL recordRenderPass(VkDevice device, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks* allocator, VkRenderPass* renderPass) {
	PassRecord record {};
	if (info->subpassCount > 0) {
		record.inputAttachments = info->pSubpasses[0].inputAttachmentCount;
	}
	for (uint32_t i = 0; i < info->dependencyCount; ++i) {
		const auto& dependency = info->pDependencies[i];
		if (dependency.srcSubpass == 0 && dependency.dstSubpass == 0 && (dependency.dstAccessMask & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT)) {
			record.selfDependencyReadsInput = true;
		}
	}
	passRecords.push_back(record);
	auto result = originalCreateRenderPass(device, info, allocator, renderPass);
	if (result == VK_SUCCESS) {
		passHasInput[*renderPass] = record.inputAttachments != 0;
	}
	return result;
}

VKAPI_ATTR VkResult VKAPI_CALL recordPipelines(VkDevice device, VkPipelineCache cache, uint32_t count, const VkGraphicsPipelineCreateInfo* infos, const VkAllocationCallbacks* allocator, VkPipeline* pipelines) {
	auto result = originalCreateGraphicsPipelines(device, cache, count, infos, allocator, pipelines);
	for (uint32_t i = 0; result == VK_SUCCESS && i < count; ++i) {
		pipelineMadeForInput[pipelines[i]] = passHasInput[infos[i].renderPass];
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL recordBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* info, VkSubpassContents contents) {
	currentPassHasInput = passHasInput[info->renderPass];
	originalCmdBeginRenderPass(commandBuffer, info, contents);
}

VKAPI_ATTR void VKAPI_CALL recordBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint, VkPipeline pipeline) {
	if (bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
		++graphicsBinds;
		auto found = pipelineMadeForInput.find(pipeline);
		if (found == pipelineMadeForInput.end() || found->second != currentPassHasInput) {
			++classMismatches;
		}
	}
	originalCmdBindPipeline(commandBuffer, bindPoint, pipeline);
}

VKAPI_ATTR VkResult VKAPI_CALL recordImage(VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks* allocator, VkImage* image) {
	imageUsages.push_back(info->usage);
	return originalCreateImage(device, info, allocator, image);
}

// ---- modules ---------------------------------------------------------------------------------------------

struct Module {
	const char* name;
	const unsigned char* spirv;
	size_t length;
	// the functions that the module has
	bool full;
};

Indium::BindingDescriptor attachmentBinding(size_t colorAttachment, size_t descriptorBinding = 0) {
	Indium::BindingDescriptor binding;
	binding.type = Indium::BindingType::InputAttachment;
	binding.index = colorAttachment;
	binding.internalIndex = descriptorBinding;
	return binding;
}

Indium::LibraryReflection reflectionOf(const Module& module) {
	Indium::LibraryReflection reflection;
	reflection.functions["fullscreen_vs"].functionType = Indium::FunctionType::Vertex;
	// as mslc's reflection says for these functions: the attachment and the descriptor binding
	reflection.functions["invert_fs"].functionType = Indium::FunctionType::Fragment;
	reflection.functions["invert_fs"].bindings.push_back(attachmentBinding(0));
	if (module.full) {
		reflection.functions["copy_from_1_fs"].functionType = Indium::FunctionType::Fragment;
		reflection.functions["copy_from_1_fs"].bindings.push_back(attachmentBinding(1));
		reflection.functions["constant_fs"].functionType = Indium::FunctionType::Fragment;
		reflection.functions["add_one_uint_fs"].functionType = Indium::FunctionType::Fragment;
		reflection.functions["add_one_uint_fs"].bindings.push_back(attachmentBinding(0));
		reflection.functions["invert_1_fs"].functionType = Indium::FunctionType::Fragment;
		reflection.functions["invert_1_fs"].bindings.push_back(attachmentBinding(1));
	}
	return reflection;
}

struct Context {
	std::shared_ptr<Indium::Device> device;
	std::shared_ptr<Indium::CommandQueue> queue;
	std::shared_ptr<Indium::Library> library;
};

std::shared_ptr<Indium::RenderPipelineState> pipelineFor(Context& context, const char* fragment, std::vector<Indium::PixelFormat> formats) {
	Indium::RenderPipelineDescriptor descriptor {};
	descriptor.vertexFunction = context.library->newFunction("fullscreen_vs");
	descriptor.fragmentFunction = context.library->newFunction(fragment);
	for (auto format: formats) {
		descriptor.colorAttachments.emplace_back();
		descriptor.colorAttachments.back().pixelFormat = format;
	}
	return context.device->newRenderPipelineState(descriptor);
}

std::shared_ptr<Indium::Texture> makeTarget(Context& context, Indium::PixelFormat format, size_t samples = 1) {
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(format, size, size, false);
	descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
	if (samples > 1) {
		descriptor.textureType = Indium::TextureType::e2DMultisample;
		descriptor.sampleCount = samples;
		descriptor.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
	}
	return context.device->newTexture(descriptor);
}

Indium::RenderPassColorAttachmentDescriptor clearTo(const std::shared_ptr<Indium::Texture>& texture, double r, double g, double b, double a) {
	Indium::RenderPassColorAttachmentDescriptor attachment {};
	attachment.texture = texture;
	attachment.loadAction = Indium::LoadAction::Clear;
	attachment.storeAction = Indium::StoreAction::Store;
	attachment.clearColor = Indium::ClearColor(r, g, b, a);
	return attachment;
}

Indium::RenderPassColorAttachmentDescriptor loaded(const std::shared_ptr<Indium::Texture>& texture) {
	Indium::RenderPassColorAttachmentDescriptor attachment {};
	attachment.texture = texture;
	attachment.loadAction = Indium::LoadAction::Load;
	attachment.storeAction = Indium::StoreAction::Store;
	return attachment;
}

// the texel at (x, y) of an RGBA8 texture, as 0xAABBGGRR
uint32_t texel(const std::shared_ptr<Indium::Texture>& texture, size_t x, size_t y) {
	std::vector<uint32_t> pixels(size * size);
	texture->getBytes(Indium::Region::make2D(0, 0, size, size), 0, pixels.data(), size * 4);
	return pixels[y * size + x];
}

bool near(uint32_t got, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	const uint8_t want[4] = { r, g, b, a };
	for (int channel = 0; channel < 4; ++channel) {
		int have = (got >> (8 * channel)) & 0xff;
		if (std::abs(have - want[channel]) > 1) {
			return false;
		}
	}
	return true;
}

std::string hex(uint32_t value) {
	char text[16];
	std::snprintf(text, sizeof(text), "%08x", value);
	return text;
}

void submit(Context& context, const Indium::RenderPassDescriptor& pass, const std::function<void(Indium::RenderCommandEncoder&)>& commands) {
	auto commandBuffer = context.queue->commandBuffer();
	auto encoder = commandBuffer->renderCommandEncoder(pass);
	commands(*encoder);
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
}

void draw(Indium::RenderCommandEncoder& encoder, const std::shared_ptr<Indium::RenderPipelineState>& pipeline) {
	encoder.setRenderPipelineState(pipeline);
	encoder.drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);
}

void checkModule(Context& context, const Module& module) {
	using Indium::PixelFormat;
	const std::string label = std::string(module.name) + ": ";
	auto invert = pipelineFor(context, "invert_fs", { PixelFormat::RGBA8Unorm });
	report(invert != nullptr, label + "a pipeline of a function that reads a color attachment is made");

	{
		auto target = makeTarget(context, PixelFormat::RGBA8Unorm);
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.push_back(clearTo(target, 0.2, 0.4, 0.6, 0.8));
		submit(context, pass, [&](auto& encoder) { draw(encoder, invert); });
		auto got = texel(target, 3, 4);
		report(near(got, 204, 153, 102, 204), label + "the function reads the cleared color and writes its inverse, got " + hex(got));
	}

	{
		auto target = makeTarget(context, PixelFormat::RGBA8Unorm);
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.push_back(clearTo(target, 0.2, 0.4, 0.6, 0.8));
		submit(context, pass, [&](auto& encoder) { draw(encoder, invert); draw(encoder, invert); });
		auto got = texel(target, 5, 2);
		report(near(got, 51, 102, 153, 204), label + "a second draw of the pass reads what the first wrote (two inversions), got " + hex(got));
	}

	{
		auto target = makeTarget(context, PixelFormat::RGBA8Unorm);
		const uint32_t start = 0xff402010; // r 0x10, g 0x20, b 0x40, a 0xff
		std::vector<uint32_t> pixels(size * size, start);
		target->replaceRegion(Indium::Region::make2D(0, 0, size, size), 0, pixels.data(), size * 4);
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.push_back(loaded(target));
		submit(context, pass, [&](auto& encoder) { draw(encoder, invert); });
		auto got = texel(target, 1, 1);
		report(near(got, 0xff - 0x10, 0xff - 0x20, 0xff - 0x40, 0xff), label + "the function reads the texture the pass loaded, got " + hex(got));
	}

	if (!module.full) {
		return;
	}

	{
		auto constant = pipelineFor(context, "constant_fs", { PixelFormat::RGBA8Unorm });
		auto target = makeTarget(context, PixelFormat::RGBA8Unorm);
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.push_back(clearTo(target, 0, 0, 0, 0));
		// a draw without fetch, then one with: the pass is switched to one that has input attachments in between
		submit(context, pass, [&](auto& encoder) { draw(encoder, constant); draw(encoder, invert); });
		auto got = texel(target, 6, 6);
		report(near(got, 191, 128, 64, 255), label + "a draw after a pipeline of another kind reads the constant it wrote, got " + hex(got));
	}

	{
		auto source = makeTarget(context, PixelFormat::RGBA8Unorm);
		auto destination = makeTarget(context, PixelFormat::RGBA8Unorm);
		auto copy = pipelineFor(context, "copy_from_1_fs", { PixelFormat::RGBA8Unorm, PixelFormat::RGBA8Unorm });
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.push_back(clearTo(destination, 0, 0, 0, 1));
		pass.colorAttachments.push_back(clearTo(source, 0.25, 0.5, 0.75, 1));
		submit(context, pass, [&](auto& encoder) { draw(encoder, copy); });
		auto got = texel(destination, 2, 2);
		auto kept = texel(source, 2, 2);
		report(near(got, 64, 128, 191, 255), label + "color(1) is the second attachment, which attachment 0 receives, got " + hex(got));
		report(near(kept, 64, 128, 191, 255), label + "the attachment that is read is unchanged, got " + hex(kept));
	}

	{
		auto empty = pipelineFor(context, "invert_1_fs", { PixelFormat::Invalid, PixelFormat::RGBA8Unorm });
		auto target = makeTarget(context, PixelFormat::RGBA8Unorm);
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.resize(2);
		pass.colorAttachments[1] = clearTo(target, 0.2, 0.4, 0.6, 0.8);
		submit(context, pass, [&](auto& encoder) { draw(encoder, empty); });
		auto got = texel(target, 0, 7);
		report(near(got, 204, 153, 102, 204), label + "color(1) is read when color attachment 0 is empty, got " + hex(got));
	}

	{
		auto other = makeTarget(context, PixelFormat::RGBA8Unorm);
		auto target = makeTarget(context, PixelFormat::RGBA8Unorm);
		auto both = pipelineFor(context, "invert_fs", { PixelFormat::RGBA8Unorm, PixelFormat::RGBA8Unorm });
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.push_back(clearTo(target, 0.2, 0.4, 0.6, 0.8));
		pass.colorAttachments.push_back(clearTo(other, 1, 1, 1, 1));
		submit(context, pass, [&](auto& encoder) { draw(encoder, both); });
		auto got = texel(target, 4, 4);
		auto untouched = texel(other, 4, 4);
		// attachment 1 is not written by the function (write mask of an attachment the function has no output for)
		report(near(got, 204, 153, 102, 204), label + "attachment 0 is read with another attachment in the pass, got " + hex(got));
		(void)untouched;
	}

	{
		auto integer = pipelineFor(context, "add_one_uint_fs", { PixelFormat::RGBA8Uint });
		auto target = makeTarget(context, PixelFormat::RGBA8Uint);
		std::vector<uint32_t> pixels(size * size, 0x04030201u);
		target->replaceRegion(Indium::Region::make2D(0, 0, size, size), 0, pixels.data(), size * 4);
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.push_back(loaded(target));
		submit(context, pass, [&](auto& encoder) { draw(encoder, integer); draw(encoder, integer); });
		auto got = texel(target, 3, 3);
		report(got == 0x06050403u, label + "an unsigned integer attachment is read and written twice (adds 2 to each channel), got " + hex(got));
	}

	{
		// the viewport and scissor rectangle set before the pipeline apply to the draw that follows it
		auto target = makeTarget(context, PixelFormat::RGBA8Unorm);
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.push_back(clearTo(target, 0.2, 0.4, 0.6, 0.8));
		submit(context, pass, [&](auto& encoder) {
			encoder.setScissorRect(Indium::ScissorRect { size, size / 2, 0, 0 });
			draw(encoder, invert);
		});
		auto inside = texel(target, 1, 4);
		auto outside = texel(target, size - 2, 4);
		report(near(inside, 204, 153, 102, 204) && near(outside, 51, 102, 153, 204), label + "the scissor rectangle set before the pipeline still applies, got " + hex(inside) + " inside and " + hex(outside) + " outside");
	}
}

}

int main() {
	Indium::init(nullptr, 0, !!getenv("INDIUM_TEST_VALIDATION"));

	{
		if (Indium::copyAllDevices().empty()) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}

		Context context;
		context.device = Indium::createSystemDefaultDevice();
		if (!Indium::DynamicVK::vkCreateRenderPass.resolve() || !Indium::DynamicVK::vkCreateImage.resolve()) {
			return 3;
		}
		originalCreateRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(Indium::DynamicVK::vkCreateRenderPass.pointer);
		Indium::DynamicVK::vkCreateRenderPass.pointer = reinterpret_cast<void*>(recordRenderPass);
		originalCreateImage = reinterpret_cast<PFN_vkCreateImage>(Indium::DynamicVK::vkCreateImage.pointer);
		Indium::DynamicVK::vkCreateImage.pointer = reinterpret_cast<void*>(recordImage);
		if (!Indium::DynamicVK::vkCreateGraphicsPipelines.resolve() || !Indium::DynamicVK::vkCmdBindPipeline.resolve() || !Indium::DynamicVK::vkCmdBeginRenderPass.resolve()) {
			return 3;
		}
		originalCreateGraphicsPipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(Indium::DynamicVK::vkCreateGraphicsPipelines.pointer);
		Indium::DynamicVK::vkCreateGraphicsPipelines.pointer = reinterpret_cast<void*>(recordPipelines);
		originalCmdBindPipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(Indium::DynamicVK::vkCmdBindPipeline.pointer);
		Indium::DynamicVK::vkCmdBindPipeline.pointer = reinterpret_cast<void*>(recordBindPipeline);
		originalCmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(Indium::DynamicVK::vkCmdBeginRenderPass.pointer);
		Indium::DynamicVK::vkCmdBeginRenderPass.pointer = reinterpret_cast<void*>(recordBeginRenderPass);
		std::printf("device: %s\n", context.device->name().c_str());

		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				context.device->pollEvents(UINT64_MAX);
			}
		});
		context.queue = context.device->newCommandQueue();

		const Module modules[] = {
			{ "mslc", fetch_spv, fetch_spv_len, true },
			{ "hand-written", handwritten_spv, handwritten_spv_len, false },
		};
		for (const auto& module: modules) {
			std::string error;
			context.library = context.device->newLibrary(module.spirv, module.length, reflectionOf(module), &error);
			report(context.library != nullptr, std::string(module.name) + ": the library with input attachment bindings loads" + (error.empty() ? "" : ": " + error));
			if (context.library) {
				checkModule(context, module);
			}
		}

		// what the usage and the passes cost
		using Indium::PixelFormat;
		{
			imageUsages.clear();
			makeTarget(context, PixelFormat::RGBA8Unorm);
			bool renderTarget = imageUsages.size() == 1 && (imageUsages[0] & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);

			imageUsages.clear();
			auto shaderOnly = Indium::TextureDescriptor::texture2DDescriptor(PixelFormat::RGBA8Unorm, size, size, false);
			shaderOnly.usage = Indium::TextureUsage::ShaderRead;
			context.device->newTexture(shaderOnly);
			bool shaderOnlyHasIt = imageUsages.size() != 1 || (imageUsages[0] & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);

			imageUsages.clear();
			makeTarget(context, PixelFormat::RGBA8Unorm, 4);
			bool multisampleHasIt = imageUsages.size() != 1 || (imageUsages[0] & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);

			imageUsages.clear();
			auto depth = Indium::TextureDescriptor::texture2DDescriptor(PixelFormat::Depth32Float, size, size, false);
			depth.usage = Indium::TextureUsage::RenderTarget;
			context.device->newTexture(depth);
			bool depthHasIt = imageUsages.size() != 1 || (imageUsages[0] & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);

			report(renderTarget, "a single-sample color render target is made with the input attachment usage");
			report(!shaderOnlyHasIt, "a texture that is not a render target is made without it");
			report(!multisampleHasIt, "a multisample render target is made without it");
			report(!depthHasIt, "a depth render target is made without it");
		}

		context.library = context.device->newLibrary(fetch_spv, fetch_spv_len, reflectionOf(modules[0]), nullptr);
		if (context.library) {
			// a pass that nothing reads from: a format of its own, so that the pass is not one the device has cached
			auto plain = pipelineFor(context, "constant_fs", { PixelFormat::RGBA16Float });
			auto target = makeTarget(context, PixelFormat::RGBA16Float);
			passRecords.clear();
			Indium::RenderPassDescriptor pass {};
			pass.colorAttachments.push_back(clearTo(target, 0, 0, 0, 0));
			submit(context, pass, [&](auto& encoder) { draw(encoder, plain); draw(encoder, plain); });
			bool noInput = !passRecords.empty();
			for (const auto& record: passRecords) {
				noInput = noInput && record.inputAttachments == 0 && !record.selfDependencyReadsInput;
			}
			report(noInput, "a pass that no fetching pipeline is set in has no input attachment references or access");

			auto fetching = pipelineFor(context, "invert_fs", { PixelFormat::RGBA32Float });
			auto floatTarget = makeTarget(context, PixelFormat::RGBA32Float);
			passRecords.clear();
			Indium::RenderPassDescriptor fetchPass {};
			fetchPass.colorAttachments.push_back(clearTo(floatTarget, 0.25, 0.5, 0.75, 1));
			submit(context, fetchPass, [&](auto& encoder) { draw(encoder, fetching); });
			size_t withInput = 0;
			size_t without = 0;
			for (const auto& record: passRecords) {
				(record.inputAttachments == 1 && record.selfDependencyReadsInput ? withInput : without) += 1;
			}
			report(withInput == 1 && without == 1, "a pass that a fetching pipeline is set in is made once with input attachment references and the self-dependency for them (after the one it replaces)");
			float pixel[4] {};
			floatTarget->getBytes(Indium::Region::make2D(0, 0, 1, 1), 0, pixel, sizeof(pixel));
			report(std::fabs(pixel[0] - 0.75f) < 1e-6f && std::fabs(pixel[1] - 0.5f) < 1e-6f && std::fabs(pixel[2] - 0.25f) < 1e-6f && pixel[3] == 1.0f, "a 32-bit float attachment is read without loss");
		}

		// pipelines and the class of the pass they are bound in
		if (context.library) {
			auto fetching = pipelineFor(context, "invert_fs", { PixelFormat::RGBA8Unorm });
			auto plain = pipelineFor(context, "constant_fs", { PixelFormat::RGBA8Unorm });
			auto bound = [&](const std::vector<std::shared_ptr<Indium::RenderPipelineState>>& order, uint32_t& pixel) {
				auto target = makeTarget(context, PixelFormat::RGBA8Unorm);
				Indium::RenderPassDescriptor pass {};
				pass.colorAttachments.push_back(clearTo(target, 0.2, 0.4, 0.6, 0.8));
				graphicsBinds = classMismatches = 0;
				submit(context, pass, [&](auto& encoder) { for (const auto& pipeline: order) draw(encoder, pipeline); });
				pixel = texel(target, 3, 3);
			};
			uint32_t pixel = 0;
			bound({ plain, fetching, plain, fetching }, pixel);
			report(graphicsBinds == 4 && classMismatches == 0, "plain, fetching, plain, fetching in one pass: every pipeline bound was made for a pass of the class of the pass it is bound in");
			report(near(pixel, 191, 128, 64, 255), "and the last draw read what the one before it wrote, got " + hex(pixel));
			bound({ fetching, plain }, pixel);
			report(graphicsBinds == 2 && classMismatches == 0, "a plain draw after a fetching one is bound with a pipeline of its pass's class");
			bound({ plain, plain }, pixel);
			report(graphicsBinds == 2 && classMismatches == 0, "plain draws only: the pipelines are made for the plain class");
			bound({ plain, fetching }, pixel);
			report(graphicsBinds == 2 && classMismatches == 0, "a fetching draw after a plain one is bound with a pipeline of its pass's class");
			// the same pipeline objects in a second pass, in the other order
			bound({ fetching, plain }, pixel);
			report(graphicsBinds == 2 && classMismatches == 0, "pipelines used before are bound again in a new pass with those of the right class");
		}

		// refusals
		if (context.library) {
			auto invert = pipelineFor(context, "invert_fs", { PixelFormat::RGBA8Unorm });
			auto commandBuffer = context.queue->commandBuffer();
			{
				Indium::RenderPassDescriptor pass {};
				pass.colorAttachments.resize(1);
				auto depth = Indium::TextureDescriptor::texture2DDescriptor(PixelFormat::Depth32Float, size, size, false);
				depth.usage = Indium::TextureUsage::RenderTarget;
				Indium::RenderPassDepthAttachmentDescriptor depthAttachment {};
				depthAttachment.texture = context.device->newTexture(depth);
				depthAttachment.loadAction = Indium::LoadAction::Clear;
				depthAttachment.storeAction = Indium::StoreAction::Store;
				depthAttachment.clearDepth = 1;
				pass.depthAttachment = depthAttachment;
				auto encoder = commandBuffer->renderCommandEncoder(pass);
				bool refused = false;
				try {
					encoder->setRenderPipelineState(invert);
				} catch (const std::runtime_error& error) {
					refused = std::string(error.what()).find("color attachment 0") != std::string::npos;
				}
				encoder->endEncoding();
				report(refused, "reading a color attachment that the pass does not have is refused");
			}
			{
				Indium::RenderPassDescriptor pass {};
				pass.colorAttachments.push_back(clearTo(makeTarget(context, PixelFormat::RGBA8Unorm, 4), 0, 0, 0, 0));
				auto encoder = commandBuffer->renderCommandEncoder(pass);
				bool refused = false;
				try {
					encoder->setRenderPipelineState(invert);
				} catch (const std::runtime_error& error) {
					refused = std::string(error.what()).find("multisampled") != std::string::npos;
				}
				encoder->endEncoding();
				report(refused, "reading a multisample color attachment is refused");
			}
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
		}

		{
			auto library = [&](const std::function<void(Indium::LibraryReflection&)>& change) {
				auto reflection = reflectionOf(modules[0]);
				change(reflection);
				std::string error;
				auto made = context.device->newLibrary(fetch_spv, fetch_spv_len, reflection, &error);
				return std::make_pair(made != nullptr, error);
			};
			auto vertex = library([](auto& reflection) { reflection.functions["fullscreen_vs"].bindings.push_back(attachmentBinding(0)); });
			report(!vertex.first && !vertex.second.empty(), "an input attachment binding of a vertex function is refused: " + vertex.second);
			auto beyond = library([](auto& reflection) { reflection.functions["invert_fs"].bindings[0].index = 8; });
			report(!beyond.first && !beyond.second.empty(), "an input attachment of color attachment 8 is refused: " + beyond.second);
			auto twice = library([](auto& reflection) { reflection.functions["invert_fs"].bindings.push_back(attachmentBinding(1, 0)); });
			report(!twice.first && !twice.second.empty(), "two input attachments at one descriptor binding are refused: " + twice.second);
		}

		keepPollingDevice = false;
		context.device->wakeupEventLoop();
		devicePollingThread.join();
		context.library.reset();
		context.queue.reset();
		context.device.reset();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
