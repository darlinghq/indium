// Copyright (C) 2026 Lubos Dolezel
//
// Visibility results: the draws after setVisibilityResultMode() are counted into the 8-byte slot of the render
// pass's visibility result buffer at the offset, once the command buffer has completed.
//  - Counting stores the number of samples that passed the depth test, were not discarded and were inside the
//    scissor rectangle; Boolean stores 1 if any passed (and leaves the slot at 0 otherwise);
//  - a slot is cleared by the first call of an encoder that uses it, and a later call for it in the same encoder
//    adds a Counting span to it (and a Boolean span that passed sets it to 1);
//  - slots no call named, and Disabled spans, leave the buffer alone; a new encoder clears the slots it names
//    instead of adding to what the buffer held.
// The expected values are the coverage of the rectangles drawn: the target is 16x16 pixels and every rectangle has
// pixel-aligned edges, so a rectangle of w x h pixels covers w * h samples exactly.
// The shaders are mslc's output for graphics.metal in this directory.
// Prints SKIP and exits 0 without a Vulkan device.

#include "graphics-spv.h"

#include <indium/indium.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
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
constexpr size_t slots = 8;
constexpr uint64_t untouched = 0xABABABABABABABABull;

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
		auto& vertexReflection = reflection.functions["quadVertex"];
		vertexReflection.functionType = Indium::FunctionType::Vertex;
		Indium::BindingDescriptor binding;
		binding.index = 0;
		binding.internalIndex = 0;
		binding.type = Indium::BindingType::Buffer;
		vertexReflection.bindings.push_back(binding);
		reflection.functions["redFragment"].functionType = Indium::FunctionType::Fragment;
		reflection.functions["discardFragment"].functionType = Indium::FunctionType::Fragment;
		std::string error;
		auto library = device->newLibrary(graphics_spv, graphics_spv_len, reflection, &error);
		report(library != nullptr, "the module loads");
		if (library) {
			auto makePipeline = [&](const char* fragment) {
				Indium::RenderPipelineDescriptor descriptor {};
				descriptor.vertexFunction = library->newFunction("quadVertex");
				descriptor.fragmentFunction = library->newFunction(fragment);
				descriptor.colorAttachments.emplace_back();
				descriptor.colorAttachments[0].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
				descriptor.depthAttachmentPixelFormat = Indium::PixelFormat::Depth32Float;
				return device->newRenderPipelineState(descriptor);
			};
			auto red = makePipeline("redFragment");
			auto discarding = makePipeline("discardFragment");

			Indium::DepthStencilDescriptor depthDescriptor;
			depthDescriptor.depthCompareFunction = Indium::CompareFunction::Always;
			depthDescriptor.depthWriteEnabled = true;
			auto always = device->newDepthStencilState(depthDescriptor);
			depthDescriptor.depthCompareFunction = Indium::CompareFunction::Less;
			auto less = device->newDepthStencilState(depthDescriptor);
			depthDescriptor.depthWriteEnabled = false;
			auto noDepthTest = device->newDepthStencilState(Indium::DepthStencilDescriptor {});

			auto textureDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, size, size, false);
			textureDescriptor.usage = Indium::TextureUsage::RenderTarget;
			auto color = device->newTexture(textureDescriptor);
			textureDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::Depth32Float, size, size, false);
			textureDescriptor.usage = Indium::TextureUsage::RenderTarget;
			auto depth = device->newTexture(textureDescriptor);

			auto newBuffer = [&](size_t length, uint8_t fill) {
				auto buffer = device->newBuffer(length, Indium::ResourceOptions::StorageModeShared);
				std::memset(buffer->contents(), fill, length);
				return buffer;
			};
			auto slot = [](const std::shared_ptr<Indium::Buffer>& buffer, size_t index) {
				uint64_t value;
				std::memcpy(&value, static_cast<char*>(buffer->contents()) + index * 8, 8);
				return value;
			};

			auto passFor = [&](const std::shared_ptr<Indium::Buffer>& results, bool loadDepth = false) {
				Indium::RenderPassDescriptor pass {};
				pass.colorAttachments.emplace_back();
				pass.colorAttachments[0].texture = color;
				pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
				pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
				pass.colorAttachments[0].clearColor = Indium::ClearColor(0, 0, 0, 0);
				pass.depthAttachment.emplace();
				pass.depthAttachment->texture = depth;
				pass.depthAttachment->loadAction = loadDepth ? Indium::LoadAction::Load : Indium::LoadAction::Clear;
				pass.depthAttachment->storeAction = Indium::StoreAction::Store;
				pass.depthAttachment->clearDepth = 1.0;
				pass.visibilityResultBuffer = results;
				return pass;
			};

			// a rectangle (x0, y0) to (x1, y1) of the target in pixels, at depth z: two triangles that never overlap
			auto quad = [](Indium::RenderCommandEncoder& encoder, int x0, int y0, int x1, int y1, float z) {
				float rectangle[5] = { x0 / 8.0f - 1, 1 - y0 / 8.0f, x1 / 8.0f - 1, 1 - y1 / 8.0f, z };
				encoder.setVertexBytes(rectangle, sizeof(rectangle), 0);
				encoder.drawPrimitives(Indium::PrimitiveType::Triangle, 0, 6);
			};

			using Mode = Indium::VisibilityResultMode;
			auto begin = [&](const std::shared_ptr<Indium::CommandBuffer>& commands, const std::shared_ptr<Indium::Buffer>& results, bool loadDepth = false) {
				auto encoder = commands->renderCommandEncoder(passFor(results, loadDepth));
				encoder->setRenderPipelineState(red);
				encoder->setDepthStencilState(always);
				return encoder;
			};
			// one encoder in one command buffer, committed and waited for
			auto run = [&](const std::shared_ptr<Indium::Buffer>& results, const std::function<void(Indium::RenderCommandEncoder&)>& body) {
				auto commands = queue->commandBuffer();
				auto encoder = begin(commands, results);
				body(*encoder);
				encoder->endEncoding();
				commands->commit();
				commands->waitUntilCompleted();
			};

			// --- Counting
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 8);
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				report(slot(results, 1) == 128, "counting counts the covered samples: an 8x16 rectangle is 128");
				report(slot(results, 0) == untouched && slot(results, 2) == untouched && slot(results, 3) == untouched, "the slots around the offset are left alone");
			}
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				report(slot(results, 0) == 256, "two draws in one span add up (overdraw is counted without a depth test)");
			}
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setDepthStencilState(less);
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 4, 0, 12, 16, 0.75f);
					e.setVisibilityResultMode(Mode::Counting, 8);
					quad(e, 0, 0, 8, 16, 0.75f);
				});
				report(slot(results, 0) == 64, "samples that fail the depth test are not counted: columns 8 to 11 of 4 to 11 pass");
				report(slot(results, 1) == 0, "a span in which every sample fails the depth test counts 0");
			}
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setRenderPipelineState(discarding);
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 4, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Counting, 8);
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				report(slot(results, 0) == 0 && slot(results, 1) == 64, "discarded fragments are not counted");
			}
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setScissorRect(Indium::ScissorRect { 16, 4, 0, 0 });
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				report(slot(results, 0) == 64, "samples outside the scissor rectangle are not counted");
			}
			{
				// the depth test off: depth is not tested and everything counts
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					quad(e, 0, 0, 16, 16, 0.25f);
					e.setDepthStencilState(noDepthTest);
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 16, 16, 0.75f);
				});
				report(slot(results, 0) == 256, "with the depth test off every covered sample counts");
			}
			{
				// the depth buffer of an earlier encoder (a pass that loads it) decides
				auto results = newBuffer(64, 0xAB);
				auto commands = queue->commandBuffer();
				auto first = begin(commands, results);
				quad(*first, 0, 0, 16, 16, 0.5f);
				first->endEncoding();
				auto second = commands->renderCommandEncoder(passFor(results, true));
				second->setRenderPipelineState(red);
				second->setDepthStencilState(less);
				second->setVisibilityResultMode(Mode::Counting, 0);
				quad(*second, 0, 0, 16, 16, 0.25f);
				second->setVisibilityResultMode(Mode::Counting, 8);
				quad(*second, 0, 0, 16, 16, 0.75f);
				second->endEncoding();
				commands->commit();
				commands->waitUntilCompleted();
				report(slot(results, 0) == 256 && slot(results, 1) == 0, "the depth values stored by an earlier pass decide what passes");
			}

			// --- Boolean
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Boolean, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Boolean, 8);
					e.setDepthStencilState(less);
					quad(e, 0, 0, 8, 16, 0.75f);
				});
				report(slot(results, 0) == 1, "a boolean span in which a sample passed stores 1, not the count");
				report(slot(results, 1) == 0, "a boolean span in which nothing passed stores 0");
			}
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Boolean, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Boolean, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Boolean, 8);
					quad(e, 0, 0, 8, 16, 0.25f);
					e.setDepthStencilState(less);
					e.setVisibilityResultMode(Mode::Boolean, 8);
					quad(e, 0, 0, 8, 16, 0.75f);
				});
				report(slot(results, 0) == 1, "two boolean spans that passed store 1, not 2");
				report(slot(results, 1) == 1, "a boolean span that failed after one that passed leaves the 1");
			}

			// --- one slot used again within an encoder
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Disabled, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 4, 16, 0.5f);
				});
				report(slot(results, 0) == 192, "a counting span after a disabled one adds to the slot: 128 + 64, the disabled draw is not counted");
			}
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Counting, 8);
					quad(e, 0, 0, 4, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 2, 16, 0.5f);
				});
				report(slot(results, 0) == 160 && slot(results, 1) == 64, "spans at alternating offsets: 128 + 32 at 0 and 64 at 8");
			}
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Boolean, 0);
					quad(e, 0, 0, 4, 16, 0.5f);
				});
				report(slot(results, 0) == 1, "a boolean span that passes after a counting span at the same slot stores 1");
				results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Boolean, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 4, 16, 0.5f);
				});
				report(slot(results, 0) == 65, "a counting span after a boolean one that passed adds its count to the 1");
			}
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Counting, 0);
				});
				report(slot(results, 0) == 128, "setting the same mode and offset again keeps what was counted");
			}

			// --- what is written and what is not
			{
				auto results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 16);
					e.setVisibilityResultMode(Mode::Boolean, 24);
				});
				report(slot(results, 2) == 0 && slot(results, 3) == 0 && slot(results, 0) == untouched, "a slot named by a span without draws is cleared to 0");
				results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 0);
					e.setVisibilityResultMode(Mode::Disabled, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				report(slot(results, 0) == 0, "a span ended by Disabled before any draw leaves 0");
				results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Disabled, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				bool untouchedAll = true;
				for (size_t i = 0; i < slots; i++) untouchedAll = untouchedAll && slot(results, i) == untouched;
				report(untouchedAll, "without a span, or with only Disabled, the buffer is left alone");
			}
			{
				// a new encoder clears the slots it names instead of adding to what is there
				auto results = newBuffer(64, 0);
				std::memset(results->contents(), 0, 64);
				uint64_t thousand = 1000;
				std::memcpy(results->contents(), &thousand, 8);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				report(slot(results, 0) == 128, "the result replaces what the buffer held");
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 4, 16, 0.5f);
				});
				report(slot(results, 0) == 64, "a second command buffer replaces the first one's result");
				run(results, [&](auto& e) {
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				report(slot(results, 0) == 64, "an encoder that names no slot leaves the earlier results");
			}
			{
				// two encoders of one command buffer: the later one replaces the slots it names only
				auto results = newBuffer(64, 0xAB);
				auto commands = queue->commandBuffer();
				auto first = begin(commands, results);
				first->setVisibilityResultMode(Mode::Counting, 0);
				quad(*first, 0, 0, 8, 16, 0.5f);
				first->setVisibilityResultMode(Mode::Counting, 8);
				quad(*first, 0, 0, 8, 16, 0.5f);
				first->endEncoding();
				auto second = begin(commands, results);
				second->setVisibilityResultMode(Mode::Counting, 8);
				quad(*second, 0, 0, 2, 16, 0.5f);
				second->endEncoding();
				commands->commit();
				commands->waitUntilCompleted();
				report(slot(results, 0) == 128 && slot(results, 1) == 32, "two encoders: the second replaces slot 1 and leaves slot 0");
			}
			{
				// the results are written when the command buffer executes, not when the span is set
				auto results = newBuffer(64, 0xAB);
				auto commands = queue->commandBuffer();
				auto encoder = begin(commands, results);
				encoder->setVisibilityResultMode(Mode::Counting, 0);
				quad(*encoder, 0, 0, 8, 16, 0.5f);
				encoder->endEncoding();
				report(slot(results, 0) == untouched, "nothing is written before the command buffer is committed");
				commands->commit();
				commands->waitUntilCompleted();
				report(slot(results, 0) == 128, "the result is there once the command buffer has completed");
			}

			// --- many spans: more than one pool of queries, and a long chain on one slot
			{
				constexpr int count = 600;
				auto results = newBuffer(count * 8, 0xAB);
				run(results, [&](auto& e) {
					for (int i = 0; i < count; i++) {
						e.setVisibilityResultMode(Mode::Counting, i * 8);
						quad(e, i % 16, 0, i % 16 + 1, 1 + i % 16, 0.5f);
					}
				});
				bool ok = true;
				for (int i = 0; i < count; i++) ok = ok && slot(results, i) == uint64_t(1 + i % 16);
				report(ok, "600 spans at 600 offsets each count their own rectangle");
				results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					for (int i = 0; i < count; i++) {
						e.setVisibilityResultMode(Mode::Counting, 0);
						quad(e, 0, 0, 1, 1, 0.5f);
					}
				});
				report(slot(results, 0) == count, "600 spans at one offset add up");
			}

			// --- offsets and buffers that are not usable
			{
				// 60 bytes: slot 7 does not fit, and neither does anything at or past the end
				auto results = newBuffer(60, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 56);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Counting, 64);
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				bool untouchedAll = true;
				for (size_t i = 0; i < slots - 1; i++) untouchedAll = untouchedAll && slot(results, i) == untouched;
				uint32_t tail;
				std::memcpy(&tail, static_cast<char*>(results->contents()) + 56, 4);
				untouchedAll = untouchedAll && tail == 0xABABABABu;
				report(untouchedAll, "an offset with less than 8 bytes left in the buffer writes nothing");
				// a mode set with an offset that is not a multiple of 8 is rounded down to one
				results = newBuffer(64, 0xAB);
				run(results, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 12);
					quad(e, 0, 0, 8, 16, 0.5f);
				});
				report(slot(results, 1) == 128 && slot(results, 0) == untouched && slot(results, 2) == untouched, "an offset that is not a multiple of 8 is rounded down");
				// no visibility result buffer: the call has nowhere to put results and does nothing
				run(nullptr, [&](auto& e) {
					e.setVisibilityResultMode(Mode::Counting, 0);
					quad(e, 0, 0, 8, 16, 0.5f);
					e.setVisibilityResultMode(Mode::Disabled, 0);
				});
				report(true, "a mode without a visibility result buffer is ignored");
			}
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
