// Copyright (C) 2026 Lubos Dolezel
//
// Indirect dispatches and draws: the GPU reads the threadgroup count or the draw arguments from a buffer.
//  - dispatchThreadgroups(indirectBuffer, ...) runs the threadgroups the buffer names, whether the host or an
//    earlier dispatch of the same encoder wrote the arguments, and from a non-zero offset;
//  - drawPrimitives / drawIndexedPrimitives with an indirect buffer draw what the arguments say: vertex and
//    index counts, a zero instance count draws nothing, first vertex and first index are honoured;
//  - arguments that are misaligned or outside the buffer are not executed.
// The shaders are mslc's output for compute.metal and graphics.metal in this directory.
// Prints SKIP and exits 0 without a Vulkan device.

#include "compute-spv.h"
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

constexpr size_t noIndex = SIZE_MAX;
constexpr uint32_t poison = 0xDEADBEEF;

std::shared_ptr<Indium::Library> loadLibrary(const std::shared_ptr<Indium::Device>& device, const unsigned char* spirv, size_t length, std::initializer_list<std::pair<const char*, Indium::FunctionType>> functions, bool buffer) {
	Indium::LibraryReflection reflection;
	for (auto& [name, type] : functions) {
		auto& function = reflection.functions[name];
		function.functionType = type;
		if (buffer) {
			function.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
		}
	}
	std::string error;
	auto library = device->newLibrary(spirv, length, reflection, &error);
	if (!library) {
		std::printf("newLibrary: %s\n", error.c_str());
	}
	return library;
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

		// --- indirect dispatch ---------------------------------------------------------------------
		auto computeLibrary = loadLibrary(device, compute_spv, compute_spv_len, { { "fillArguments", Indium::FunctionType::Kernel }, { "mark", Indium::FunctionType::Kernel } }, true);
		report(computeLibrary != nullptr, "the compute module loads");
		if (computeLibrary) {
			auto fill = device->newComputePipelineState(computeLibrary->newFunction("fillArguments"));
			auto mark = device->newComputePipelineState(computeLibrary->newFunction("mark"));
			constexpr size_t slots = 32;
			auto out = device->newBuffer(slots * sizeof(uint32_t), Indium::ResourceOptions::StorageModeShared);
			auto args = device->newBuffer(64, Indium::ResourceOptions::StorageModeShared);
			auto outWords = static_cast<uint32_t*>(out->contents());

			auto marked = [&]() {
				size_t n = 0;
				for (size_t i = 0; i < slots; ++i) {
					if (outWords[i] == i + 1) {
						++n;
					} else if (outWords[i] != poison) {
						return size_t(-1);
					}
				}
				return n;
			};
			auto reset = [&]() {
				for (size_t i = 0; i < slots; ++i) {
					outWords[i] = poison;
				}
			};

			// arguments written by the host, at a non-zero offset
			reset();
			auto hostArgs = static_cast<uint32_t*>(args->contents());
			std::memset(hostArgs, 0, 64);
			hostArgs[4] = 3; hostArgs[5] = 1; hostArgs[6] = 1;
			{
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(mark);
				encoder->setBuffer(out, 0, 0);
				encoder->dispatchThreadgroups(args, 16, Indium::Size { 2, 1, 1 });
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
			}
			report(marked() == 6, "an indirect dispatch of 3 threadgroups of 2 threads runs 6 threads");

			// arguments written by an earlier dispatch of the same encoder
			reset();
			std::memset(hostArgs, 0, 64);
			{
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(fill);
				encoder->setBuffer(args, 0, 0);
				encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
				encoder->setComputePipelineState(mark);
				encoder->setBuffer(out, 0, 0);
				encoder->dispatchThreadgroups(args, 0, Indium::Size { 2, 1, 1 });
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
			}
			report(marked() == 8, "an indirect dispatch sees the arguments an earlier dispatch of its encoder wrote");

			// zero threadgroups
			reset();
			std::memset(hostArgs, 0, 64);
			{
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(mark);
				encoder->setBuffer(out, 0, 0);
				encoder->dispatchThreadgroups(args, 0, Indium::Size { 2, 1, 1 });
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
			}
			report(marked() == 0, "an indirect dispatch of zero threadgroups runs nothing");

			// misaligned and out-of-range arguments are not executed
			reset();
			hostArgs[0] = 4; hostArgs[1] = 1; hostArgs[2] = 1;
			{
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				encoder->setComputePipelineState(mark);
				encoder->setBuffer(out, 0, 0);
				encoder->dispatchThreadgroups(args, 2, Indium::Size { 2, 1, 1 });   // not a multiple of 4
				encoder->dispatchThreadgroups(args, 56, Indium::Size { 2, 1, 1 });  // 12 bytes do not fit in the last 8
				encoder->dispatchThreadgroups(args, 4096, Indium::Size { 2, 1, 1 }); // far outside
				encoder->dispatchThreadgroups(nullptr, 0, Indium::Size { 2, 1, 1 });
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
			}
			report(marked() == 0, "misaligned, overrunning and missing indirect arguments dispatch nothing");
		}

		// --- indirect draws --------------------------------------------------------------------------
		auto graphicsLibrary = loadLibrary(device, graphics_spv, graphics_spv_len, { { "triangleVertex", Indium::FunctionType::Vertex }, { "redFragment", Indium::FunctionType::Fragment } }, false);
		report(graphicsLibrary != nullptr, "the graphics module loads");
		if (graphicsLibrary) {
			Indium::RenderPipelineDescriptor psoDescriptor {};
			psoDescriptor.vertexFunction = graphicsLibrary->newFunction("triangleVertex");
			psoDescriptor.fragmentFunction = graphicsLibrary->newFunction("redFragment");
			psoDescriptor.colorAttachments.emplace_back();
			psoDescriptor.colorAttachments[0].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
			auto pso = device->newRenderPipelineState(psoDescriptor);

			constexpr size_t size = 8;
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, size, size, false);
			descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
			auto target = device->newTexture(descriptor);

			// the number of red pixels after one draw
			auto redPixels = [&](const std::function<void(Indium::RenderCommandEncoder&)>& draw) {
				Indium::RenderPassDescriptor pass {};
				pass.colorAttachments.emplace_back();
				pass.colorAttachments[0].texture = target;
				pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
				pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
				pass.colorAttachments[0].clearColor = Indium::ClearColor(0, 0, 0, 0);
				auto commandBuffer = queue->commandBuffer();
				auto encoder = commandBuffer->renderCommandEncoder(pass);
				encoder->setRenderPipelineState(pso);
				draw(*encoder);
				encoder->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				std::vector<uint32_t> pixels(size * size);
				target->getBytes(Indium::Region::make2D(0, 0, size, size), 0, pixels.data(), size * 4);
				size_t red = 0;
				for (auto pixel : pixels) {
					if (pixel == 0xFF0000FFu) {
						++red;
					} else if (pixel != 0) {
						return size_t(-1);
					}
				}
				return red;
			};

			auto drawArgs = device->newBuffer(64, Indium::ResourceOptions::StorageModeShared);
			auto words = static_cast<uint32_t*>(drawArgs->contents());
			auto set = [&](std::initializer_list<uint32_t> values, size_t first = 0) {
				std::memset(words, 0, 64);
				size_t i = first;
				for (auto v : values) {
					words[i++] = v;
				}
			};

			set({ 3, 1, 0, 0 });
			report(redPixels([&](auto& e) { e.drawPrimitives(Indium::PrimitiveType::Triangle, drawArgs, 0); }) == size * size, "an indirect draw of 3 vertices covers the target");

			set({ 3, 0, 0, 0 });
			report(redPixels([&](auto& e) { e.drawPrimitives(Indium::PrimitiveType::Triangle, drawArgs, 0); }) == 0, "an indirect draw with instance count 0 draws nothing");

			set({ 0, 1, 0, 0 });
			report(redPixels([&](auto& e) { e.drawPrimitives(Indium::PrimitiveType::Triangle, drawArgs, 0); }) == 0, "an indirect draw with vertex count 0 draws nothing");

			set({ 3, 1, 3, 0 });
			report(redPixels([&](auto& e) { e.drawPrimitives(Indium::PrimitiveType::Triangle, drawArgs, 0); }) == 0, "the first vertex of the arguments is honoured");

			set({ 3, 1, 0, 0 }, 4);
			report(redPixels([&](auto& e) { e.drawPrimitives(Indium::PrimitiveType::Triangle, drawArgs, 16); }) == size * size, "arguments are read from the offset");

			set({ 3, 2, 0, 0 });
			report(redPixels([&](auto& e) { e.drawPrimitives(Indium::PrimitiveType::Triangle, drawArgs, 0); }) == size * size, "two instances still draw the same covering triangle");

			set({ 3, 1, 0, 0 });
			report(redPixels([&](auto& e) {
				e.drawPrimitives(Indium::PrimitiveType::Triangle, drawArgs, 2);
				e.drawPrimitives(Indium::PrimitiveType::Triangle, drawArgs, 56);
				e.drawPrimitives(Indium::PrimitiveType::Triangle, nullptr, 0);
			}) == 0, "misaligned, overrunning and missing indirect draw arguments draw nothing");

			// indexed: the index buffer holds a degenerate triple followed by 0, 1, 2
			const uint16_t indices[6] = { 7, 7, 7, 0, 1, 2 };
			auto indexBuffer = device->newBuffer(indices, sizeof(indices), Indium::ResourceOptions::StorageModeShared);

			set({ 3, 1, 3, 0, 0 });
			report(redPixels([&](auto& e) { e.drawIndexedPrimitives(Indium::PrimitiveType::Triangle, Indium::IndexType::UInt16, indexBuffer, 0, drawArgs, 0); }) == size * size,
				"an indirect indexed draw honours the first index");

			set({ 3, 1, 0, 0, 0 });
			report(redPixels([&](auto& e) { e.drawIndexedPrimitives(Indium::PrimitiveType::Triangle, Indium::IndexType::UInt16, indexBuffer, 0, drawArgs, 0); }) == 0,
				"an indirect indexed draw from the degenerate indices draws nothing");

			set({ 3, 1, 0, 0, 0 });
			report(redPixels([&](auto& e) { e.drawIndexedPrimitives(Indium::PrimitiveType::Triangle, Indium::IndexType::UInt16, indexBuffer, 6, drawArgs, 0); }) == size * size,
				"the index buffer offset applies to the first index");

			set({ 3, 0, 3, 0, 0 });
			report(redPixels([&](auto& e) { e.drawIndexedPrimitives(Indium::PrimitiveType::Triangle, Indium::IndexType::UInt16, indexBuffer, 0, drawArgs, 0); }) == 0,
				"an indirect indexed draw with instance count 0 draws nothing");

			set({ 3, 1, 3, 0, 0 });
			report(redPixels([&](auto& e) {
				e.drawIndexedPrimitives(Indium::PrimitiveType::Triangle, Indium::IndexType::UInt16, indexBuffer, 0, drawArgs, 2);
				e.drawIndexedPrimitives(Indium::PrimitiveType::Triangle, Indium::IndexType::UInt16, indexBuffer, 0, drawArgs, 52);
			}) == 0, "misaligned and overrunning indirect indexed draw arguments draw nothing");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
