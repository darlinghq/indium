// Copyright (C) 2026 Darling Developers
//
// Per-draw data and render passes are shared instead of created for every draw or encoder:
//  - setBytes copies its bytes at the time of the call, so every dispatch of an encoder sees its
//    own constants even though they share scratch memory, including constants too large for it;
//  - encoding draws or dispatches does not allocate device memory for each one;
//  - render passes with the same description share one VkRenderPass, and passes that differ in
//    their load action do not; a Clear still clears with each pass's own clear color.
// It also times a run of draws, each with its own setVertexBytes, and counts the device memory
// allocations encoding them made ("BENCH" lines), for comparing builds; those numbers are not
// checked.

#include "air-ops.h"
#include "AAPLShaders.h"

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <thread>
#include <vector>

namespace {

constexpr size_t threadCount = 8;
constexpr size_t slots = 4;

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

std::atomic<unsigned> memoryAllocations { 0 };
PFN_vkAllocateMemory originalAllocateMemory;
VKAPI_ATTR VkResult VKAPI_CALL countAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* allocator, VkDeviceMemory* memory) {
	++memoryAllocations;
	return originalAllocateMemory(device, info, allocator, memory);
}

std::atomic<unsigned> renderPassCreations { 0 };
PFN_vkCreateRenderPass originalCreateRenderPass;
VKAPI_ATTR VkResult VKAPI_CALL countCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks* allocator, VkRenderPass* renderPass) {
	++renderPassCreations;
	return originalCreateRenderPass(device, info, allocator, renderPass);
}

// what air-ops.metal's control_flow kernel writes for each thread
std::vector<int32_t> controlFlow(const int32_t* ns) {
	std::vector<int32_t> want;
	for (size_t id = 0; id < threadCount; ++id) {
		uint32_t n = ns[id];
		uint32_t squares = 0, nested = 0, k = 0;
		for (uint32_t i = 0; i < n; ++i) {
			squares += i * i;
			for (uint32_t j = 0; j < i; ++j) {
				nested += i ^ j;
			}
		}
		for (; k < n && k * k <= id * 5; ++k);
		want.push_back(squares);
		want.push_back((n & 1) ? n * 3 + 1 : n / 2);
		want.push_back(nested);
		want.push_back(k);
	}
	return want;
}

void testSetBytes(Indium::Device* device, size_t dispatches) {
	auto library = device->newLibrary(air_ops_metallib, air_ops_metallib_len);
	auto pipeline = device->newComputePipelineState(library->newFunction("control_flow"));
	auto queue = device->newCommandQueue();
	const size_t outputSize = threadCount * slots * sizeof(int32_t);
	auto output = device->newBuffer(outputSize * dispatches, Indium::ResourceOptions::StorageModeShared);
	std::memset(output->contents(), 0xA5, outputSize * dispatches);

	// Every 7th dispatch passes constants larger than the scratch chunks take; the kernel only
	// reads the first threadCount of them.
	std::vector<std::vector<int32_t>> inputs;
	for (size_t i = 0; i < dispatches; ++i) {
		std::vector<int32_t> ns(i % 7 == 6 ? 40 * 1024 / sizeof(int32_t) : threadCount, 0x7fffffff);
		for (size_t t = 0; t < threadCount; ++t) {
			ns[t] = static_cast<int32_t>((i * 5 + t * 3) % 13);
		}
		inputs.push_back(std::move(ns));
	}

	unsigned allocationsBefore = memoryAllocations;
	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	for (size_t i = 0; i < dispatches; ++i) {
		encoder->setBuffer(output, i * outputSize, 0);
		encoder->setBytes(inputs[i].data(), inputs[i].size() * sizeof(int32_t), 1);
		// overwrite the caller's copy: setBytes must already have taken its own
		std::memset(inputs[i].data(), 0, threadCount * sizeof(int32_t));
		encoder->dispatchThreads(Indium::Size { threadCount, 1, 1 }, Indium::Size { threadCount, 1, 1 });
		for (size_t t = 0; t < threadCount; ++t) {
			inputs[i][t] = static_cast<int32_t>((i * 5 + t * 3) % 13);
		}
	}
	encoder->endEncoding();
	unsigned allocations = memoryAllocations - allocationsBefore;
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();

	size_t wrong = 0;
	for (size_t i = 0; i < dispatches; ++i) {
		auto want = controlFlow(inputs[i].data());
		if (std::memcmp(static_cast<char*>(output->contents()) + i * outputSize, want.data(), outputSize) != 0) {
			++wrong;
		}
	}

	report(wrong == 0, "every dispatch sees the constants passed to its own setBytes");
	// before: one allocation per setBytes and one per address table, at least 2 per dispatch
	report(allocations * 4 < dispatches, "encoding dispatches does not allocate memory for each one");
}

std::shared_ptr<Indium::Texture> newTarget(Indium::Device* device) {
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
	descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
	return device->newTexture(descriptor);
}

void runPass(Indium::CommandQueue* queue, std::shared_ptr<Indium::Texture> target, Indium::LoadAction load, Indium::ClearColor color) {
	Indium::RenderPassDescriptor descriptor {};
	Indium::RenderPassColorAttachmentDescriptor attachment {};
	attachment.texture = target;
	attachment.loadAction = load;
	attachment.storeAction = Indium::StoreAction::Store;
	attachment.clearColor = color;
	descriptor.colorAttachments.push_back(attachment);
	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->renderCommandEncoder(descriptor);
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
}

bool allPixels(Indium::Texture* texture, uint32_t rgba) {
	std::vector<uint32_t> pixels(16, 0);
	texture->getBytes(Indium::Region::make2D(0, 0, 4, 4), 0, pixels.data(), 4 * 4);
	for (auto pixel: pixels) {
		if (pixel != rgba) {
			return false;
		}
	}
	return true;
}

void testRenderPasses(Indium::Device* device) {
	auto queue = device->newCommandQueue();
	auto target = newTarget(device);
	auto other = newTarget(device);

	unsigned before = renderPassCreations;
	runPass(queue.get(), target, Indium::LoadAction::Clear, Indium::ClearColor(1, 0, 0, 1));
	bool red = allPixels(target.get(), 0xff0000ff);
	runPass(queue.get(), other, Indium::LoadAction::Clear, Indium::ClearColor(0, 1, 0, 1));
	bool green = allPixels(other.get(), 0xff00ff00);
	runPass(queue.get(), target, Indium::LoadAction::Clear, Indium::ClearColor(0, 0, 1, 1));
	bool blue = allPixels(target.get(), 0xffff0000);
	unsigned clearPasses = renderPassCreations - before;

	before = renderPassCreations;
	runPass(queue.get(), target, Indium::LoadAction::Load, Indium::ClearColor(1, 1, 1, 1));
	bool kept = allPixels(target.get(), 0xffff0000);
	unsigned loadPasses = renderPassCreations - before;

	report(red && green && blue, "passes sharing a render pass clear with their own clear colors");
	report(kept, "a Load pass after them keeps the contents");
	report(clearPasses <= 1, "identical render pass descriptions create one VkRenderPass");
	report(loadPasses == 1, "a different load action creates its own VkRenderPass");
}

// AAPLShaders.metallib's vertexShader takes its vertices (AAPLVertex) and the viewport size in
// pixels with setVertexBytes, and fragmentShader returns the vertex color.
struct Vertex {
	float position[2];
	float padding[2];
	float color[4];
};

// Draws each quadrant of a 64x64 target in its own color, every draw with its own setVertexBytes;
// then, if `draws` is set, times `draws` more draws in one encoder.
void testDraws(Indium::Device* device, size_t draws) {
	auto library = device->newLibrary(shader, shader_len);
	Indium::RenderPipelineDescriptor psoDescriptor {};
	psoDescriptor.vertexFunction = library->newFunction("vertexShader");
	psoDescriptor.fragmentFunction = library->newFunction("fragmentShader");
	psoDescriptor.colorAttachments.emplace_back();
	psoDescriptor.colorAttachments[0].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
	auto pso = device->newRenderPipelineState(psoDescriptor);
	auto queue = device->newCommandQueue();

	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 64, 64, false);
	descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
	auto target = device->newTexture(descriptor);

	static const float colors[4][4] { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 1, 1 } };
	auto quadrant = [](size_t q, const float* color, Vertex* vertices) {
		float x0 = (q & 1) ? 0 : -32, y0 = (q & 2) ? 0 : -32;
		const float corners[6][2] { { x0, y0 }, { x0 + 32, y0 }, { x0, y0 + 32 }, { x0 + 32, y0 }, { x0 + 32, y0 + 32 }, { x0, y0 + 32 } };
		for (size_t v = 0; v < 6; ++v) {
			vertices[v] = Vertex { { corners[v][0], corners[v][1] }, { 0, 0 }, { color[0], color[1], color[2], color[3] } };
		}
	};
	const uint32_t viewportSize[2] { 64, 64 };

	auto encode = [&](size_t count, bool check) {
		Indium::RenderPassDescriptor pass {};
		Indium::RenderPassColorAttachmentDescriptor attachment {};
		attachment.texture = target;
		attachment.loadAction = Indium::LoadAction::Clear;
		attachment.storeAction = Indium::StoreAction::Store;
		attachment.clearColor = Indium::ClearColor(0, 0, 0, 0);
		pass.colorAttachments.push_back(attachment);

		unsigned allocationsBefore = memoryAllocations;
		auto start = std::chrono::steady_clock::now();
		auto commandBuffer = queue->commandBuffer();
		auto encoder = commandBuffer->renderCommandEncoder(pass);
		encoder->setRenderPipelineState(pso);
		for (size_t i = 0; i < count; ++i) {
			Vertex vertices[6];
			quadrant(i % 4, colors[i % 4], vertices);
			encoder->setVertexBytes(vertices, sizeof(vertices), 0);
			// setBytes copied them: changing them now must not change the draw
			std::memset(vertices, 0, sizeof(vertices));
			encoder->setVertexBytes(viewportSize, sizeof(viewportSize), 1);
			encoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 6);
		}
		encoder->endEncoding();
		unsigned allocations = memoryAllocations - allocationsBefore;
		double encodeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

		if (check) {
			std::vector<uint32_t> pixels(64 * 64);
			target->getBytes(Indium::Region::make2D(0, 0, 64, 64), 0, pixels.data(), 64 * 4);
			std::set<uint32_t> seen;
			for (size_t y: { 16, 48 }) {
				for (size_t x: { 16, 48 }) {
					seen.insert(pixels[y * 64 + x]);
				}
			}
			std::set<uint32_t> want { 0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff };
			report(seen == want, "each draw uses the vertices passed to its own setVertexBytes");
			report(allocations <= 1, "small setVertexBytes of several draws share one allocation");
		} else {
			std::printf("BENCH draws=%zu encode_us_per_draw=%.1f total_us_per_draw=%.1f memory_allocations=%u\n",
				count, encodeSeconds * 1e6 / count, seconds * 1e6 / count, allocations);
		}
	};

	encode(4, true);
	for (int run = 0; draws && run < 3; ++run) {
		encode(draws, false);
	}
}

}

int main(int argc, char** argv) {
	Indium::init(nullptr, 0, false);

	{
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			return 0;
		}

		bool keepPollingDevice = true;
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		Indium::DynamicVK::vkAllocateMemory.resolve();
		originalAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(Indium::DynamicVK::vkAllocateMemory.pointer);
		Indium::DynamicVK::vkAllocateMemory.pointer = reinterpret_cast<void*>(countAllocateMemory);
		Indium::DynamicVK::vkCreateRenderPass.resolve();
		originalCreateRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(Indium::DynamicVK::vkCreateRenderPass.pointer);
		Indium::DynamicVK::vkCreateRenderPass.pointer = reinterpret_cast<void*>(countCreateRenderPass);

		testSetBytes(device.get(), 500);
		testRenderPasses(device.get());

		// benchmark: argument = draws per encoder (default 20000), three runs each
		size_t draws = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 20000;
		testDraws(device.get(), draws);

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	std::printf("%s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
