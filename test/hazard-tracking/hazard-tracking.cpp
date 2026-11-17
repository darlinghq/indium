// Copyright (C) 2026 Lubos Dolezel
//
// How the commands of a command buffer are ordered (see README.md): which barriers each sequence of encoders
// records, and whether chains of dependent work produce the right numbers.
// The shaders are mslc's output for compute.metal and graphics.metal in this directory.
// Prints SKIP and exits 0 without a Vulkan device.

#include "compute-spv.h"
#include "graphics-spv.h"

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t noIndex = SIZE_MAX;

// --- the barriers that are recorded ---------------------------------------------------------------------

struct Barrier {
	uint64_t srcStages, srcAccess, dstStages, dstAccess;
};

PFN_vkCmdPipelineBarrier2 originalBarrier2;
std::mutex barrierLock;
std::vector<Barrier> barriers;

void VKAPI_CALL recordBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* info) {
	{
		std::scoped_lock lock(barrierLock);
		for (uint32_t i = 0; i < info->memoryBarrierCount; ++i) {
			const auto& barrier = info->pMemoryBarriers[i];
			barriers.push_back(Barrier { barrier.srcStageMask, barrier.srcAccessMask, barrier.dstStageMask, barrier.dstAccessMask });
		}
	}
	originalBarrier2(commandBuffer, info);
}

constexpr uint64_t compute = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr uint64_t transfer = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
constexpr uint64_t colorOutput = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr uint64_t drawIndirect = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
constexpr uint64_t allCommands = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

void forgetBarriers() {
	std::scoped_lock lock(barrierLock);
	barriers.clear();
}

std::vector<Barrier> recordedBarriers() {
	std::scoped_lock lock(barrierLock);
	return barriers;
}

// the barriers recorded while `work` runs
std::vector<Barrier> observe(const std::function<void()>& work) {
	forgetBarriers();
	work();
	return recordedBarriers();
}

// barriers that order the stages `src` before the stages `dst`: not the ones from everything that came before the
// command buffer (see `imports`), and not the full ones
size_t between(const std::vector<Barrier>& recorded, uint64_t src, uint64_t dst) {
	size_t count = 0;
	for (const auto& barrier: recorded) {
		if ((barrier.srcStages & src) && (barrier.dstStages & dst) && !(barrier.srcStages & allCommands)) {
			++count;
		}
	}
	return count;
}

// barriers from everything that came before the command buffer
size_t imports(const std::vector<Barrier>& recorded) {
	size_t count = 0;
	for (const auto& barrier: recorded) {
		if ((barrier.srcStages & allCommands) && !(barrier.dstStages & allCommands)) {
			++count;
		}
	}
	return count;
}

// barriers from everything that came before the command buffer to an access of the kind `access`
size_t importsFor(const std::vector<Barrier>& recorded, uint64_t access) {
	size_t count = 0;
	for (const auto& barrier: recorded) {
		if ((barrier.srcStages & allCommands) && !(barrier.dstStages & allCommands) && (barrier.dstAccess & access)) {
			++count;
		}
	}
	return count;
}

// barriers from all commands to all commands
size_t fullBarriers(const std::vector<Barrier>& recorded) {
	size_t count = 0;
	for (const auto& barrier: recorded) {
		if ((barrier.srcStages & allCommands) && (barrier.dstStages & allCommands)) {
			++count;
		}
	}
	return count;
}

// --- the device -----------------------------------------------------------------------------------------

constexpr size_t elements = 1024;
constexpr size_t bytes = elements * sizeof(uint32_t);
constexpr size_t threadgroup = 64;

std::shared_ptr<Indium::Library> loadLibrary(const std::shared_ptr<Indium::Device>& device, const unsigned char* spirv, size_t length, std::initializer_list<std::tuple<const char*, Indium::FunctionType, size_t>> functions) {
	Indium::LibraryReflection reflection;
	for (auto& [name, type, buffers]: functions) {
		auto& function = reflection.functions[name];
		function.functionType = type;
		for (size_t i = 0; i < buffers; ++i) {
			function.bindings.push_back(Indium::BindingDescriptor { i, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
		}
	}
	std::string error;
	auto library = device->newLibrary(spirv, length, reflection, &error);
	if (!library) {
		std::printf("newLibrary: %s\n", error.c_str());
	}
	return library;
}

struct Rig {
	std::shared_ptr<Indium::Device> device;
	std::shared_ptr<Indium::CommandQueue> queue;
	std::shared_ptr<Indium::ComputePipelineState> step, addOne, fillDrawArguments;
	std::shared_ptr<Indium::RenderPipelineState> redTriangle;

	std::shared_ptr<Indium::Buffer> buffer(size_t length = bytes, bool tracked = true) const {
		return device->newBuffer(length, Indium::ResourceOptions::StorageModeShared |
			(tracked ? Indium::ResourceOptions::HazardTrackingModeTracked : Indium::ResourceOptions::HazardTrackingModeUntracked));
	}

	// a buffer holding 0, 1, 2, ...
	std::shared_ptr<Indium::Buffer> counting(bool tracked = true) const {
		auto result = buffer(bytes, tracked);
		auto words = static_cast<uint32_t*>(result->contents());
		for (size_t i = 0; i < elements; ++i) {
			words[i] = static_cast<uint32_t>(i);
		}
		return result;
	}

	void dispatch(Indium::ComputeCommandEncoder& encoder, const std::shared_ptr<Indium::ComputePipelineState>& pipeline, std::initializer_list<std::shared_ptr<Indium::Buffer>> buffers) const {
		encoder.setComputePipelineState(pipeline);
		size_t index = 0;
		for (auto& buffer: buffers) {
			encoder.setBuffer(buffer, 0, index++);
		}
		encoder.dispatchThreadgroups(Indium::Size { elements / threadgroup, 1, 1 }, Indium::Size { threadgroup, 1, 1 });
	}

	void run(const std::function<void(Indium::CommandBuffer&)>& encode) const {
		auto commandBuffer = queue->commandBuffer();
		encode(*commandBuffer);
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
	}
};

// the value of element `index` of a counting buffer after `n` steps (data * 3 + 1)
uint32_t stepped(size_t index, size_t n) {
	uint32_t value = static_cast<uint32_t>(index);
	for (size_t i = 0; i < n; ++i) {
		value = value * 3 + 1;
	}
	return value;
}

bool holdsStepped(const std::shared_ptr<Indium::Buffer>& buffer, size_t n) {
	auto words = static_cast<const uint32_t*>(buffer->contents());
	for (size_t i = 0; i < elements; ++i) {
		if (words[i] != stepped(i, n)) {
			return false;
		}
	}
	return true;
}

constexpr size_t targetSize = 8;
constexpr uint32_t red = 0xFF0000FFu;

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

		Rig rig;
		rig.device = Indium::createSystemDefaultDevice();
		auto& device = rig.device;
		std::printf("device: %s\n", device->name().c_str());
		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});
		rig.queue = device->newCommandQueue();

		auto computeLibrary = loadLibrary(device, compute_spv, compute_spv_len, {
			{ "step", Indium::FunctionType::Kernel, 1 },
			{ "addOne", Indium::FunctionType::Kernel, 2 },
			{ "fillDrawArguments", Indium::FunctionType::Kernel, 1 },
		});
		auto graphicsLibrary = loadLibrary(device, graphics_spv, graphics_spv_len, {
			{ "triangleVertex", Indium::FunctionType::Vertex, 0 },
			{ "redFragment", Indium::FunctionType::Fragment, 0 },
		});
		report(computeLibrary && graphicsLibrary, "the modules load");
		if (!computeLibrary || !graphicsLibrary) {
			keepPollingDevice = false;
			device->wakeupEventLoop();
			devicePollingThread.join();
			Indium::finit();
			return 1;
		}
		rig.step = device->newComputePipelineState(computeLibrary->newFunction("step"));
		rig.addOne = device->newComputePipelineState(computeLibrary->newFunction("addOne"));
		rig.fillDrawArguments = device->newComputePipelineState(computeLibrary->newFunction("fillDrawArguments"));
		Indium::RenderPipelineDescriptor psoDescriptor {};
		psoDescriptor.vertexFunction = graphicsLibrary->newFunction("triangleVertex");
		psoDescriptor.fragmentFunction = graphicsLibrary->newFunction("redFragment");
		psoDescriptor.colorAttachments.emplace_back();
		psoDescriptor.colorAttachments[0].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
		rig.redTriangle = device->newRenderPipelineState(psoDescriptor);

		Indium::DynamicVK::vkCmdPipelineBarrier2.resolve();
		originalBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(Indium::DynamicVK::vkCmdPipelineBarrier2.pointer);
		Indium::DynamicVK::vkCmdPipelineBarrier2.pointer = reinterpret_cast<void*>(recordBarrier2);

		auto copyBuffer = [](Indium::CommandBuffer& cb, const std::shared_ptr<Indium::Buffer>& from, const std::shared_ptr<Indium::Buffer>& to) {
			auto blit = cb.blitCommandEncoder();
			blit->copy(from, 0, to, 0, bytes);
			blit->endEncoding();
		};
		auto fillBuffer = [](Indium::CommandBuffer& cb, const std::shared_ptr<Indium::Buffer>& buffer, uint8_t value) {
			auto blit = cb.blitCommandEncoder();
			blit->fillBuffer(buffer, Indium::Range<size_t> { 0, bytes }, value);
			blit->endEncoding();
		};

		// ---------------------------------------------------------------------------------------------
		// the barriers
		// ---------------------------------------------------------------------------------------------

		{
			auto written = rig.counting();
			auto copy = rig.buffer();
			auto other = rig.buffer();
			auto otherCopy = rig.buffer();

			auto recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder();
					rig.dispatch(*encoder, rig.step, { written });
					encoder->endEncoding();
					copyBuffer(cb, written, copy);
				});
			});
			report(between(recorded, compute, transfer) == 1, "read after write: a copy of what a dispatch wrote waits for the dispatch");
			report(holdsStepped(copy, 1), "read after write: the copy holds what the dispatch wrote");

			recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder();
					rig.dispatch(*encoder, rig.step, { written });
					encoder->endEncoding();
					copyBuffer(cb, other, otherCopy);
				});
			});
			report(between(recorded, compute, transfer) == 0, "an encoder that touches other memory does not wait for the dispatch");
		}

		{
			// memory this command buffer has not seen was written by the command buffers before it
			auto shared = rig.counting();
			auto copyOfShared = rig.buffer();
			rig.run([&](Indium::CommandBuffer& cb) {
				auto encoder = cb.computeCommandEncoder();
				rig.dispatch(*encoder, rig.step, { shared });
				encoder->endEncoding();
			});
			auto recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					copyBuffer(cb, shared, copyOfShared);
				});
			});
			report(importsFor(recorded, VK_ACCESS_2_TRANSFER_READ_BIT) == 1 && importsFor(recorded, VK_ACCESS_2_TRANSFER_WRITE_BIT) == 1,
				"a command buffer orders the first read and the first write of memory after what came before it");
			report(holdsStepped(copyOfShared, 1), "a command buffer sees what the one before it wrote");
		}

		{
			auto source = rig.counting();
			auto first = rig.buffer();
			auto second = rig.buffer();
			auto recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					copyBuffer(cb, source, first);
					copyBuffer(cb, source, second);
				});
			});
			report(between(recorded, transfer, transfer) == 0, "read after read: two copies from one buffer do not wait for each other");

			recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					copyBuffer(cb, source, first);
					fillBuffer(cb, source, 0xAB);
				});
			});
			auto war = between(recorded, transfer, transfer);
			report(war == 1, "write after read: a fill of a buffer waits for the copy that read it");
			bool executionOnly = true;
			for (const auto& barrier: recorded) {
				if ((barrier.srcStages & transfer) && !(barrier.srcStages & allCommands) && barrier.srcAccess != 0) {
					executionOnly = false;
				}
			}
			report(executionOnly, "write after read: the dependency is an execution dependency, nothing has to be made available");
			report(static_cast<const uint8_t*>(source->contents())[17] == 0xAB, "write after read: the fill happened");

			auto target = rig.buffer();
			recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					fillBuffer(cb, target, 1);
					fillBuffer(cb, target, 2);
				});
			});
			report(between(recorded, transfer, transfer) == 1, "write after write: a second fill of a buffer waits for the first");
			report(static_cast<const uint8_t*>(target->contents())[5] == 2, "write after write: the last fill wins");
		}

		{
			// many encoders over memory of their own: no dependency is recorded between them
			std::vector<std::shared_ptr<Indium::Buffer>> sources, destinations;
			for (int i = 0; i < 20; ++i) {
				sources.push_back(rig.counting());
				destinations.push_back(rig.buffer());
			}
			auto recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					for (int i = 0; i < 20; ++i) {
						copyBuffer(cb, sources[i], destinations[i]);
					}
				});
			});
			// the first use of memory the command buffer has not seen is ordered after what came before, once for all
			// of it
			report(between(recorded, transfer, transfer) == 0 && recorded.size() <= 2, "twenty encoders over memory of their own record no dependency between them");
		}

		{
			auto data = rig.counting();
			auto otherData = rig.counting();
			// a serial encoder orders its dispatches by what they access
			auto recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Serial);
					for (int i = 0; i < 3; ++i) {
						rig.dispatch(*encoder, rig.step, { data });
					}
					encoder->endEncoding();
				});
			});
			report(between(recorded, compute, compute) == 2, "serial: each dispatch waits for the one before it over the same buffer");
			report(holdsStepped(data, 3), "serial: the dispatches saw each other's writes");

			recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Serial);
					rig.dispatch(*encoder, rig.step, { data });
					rig.dispatch(*encoder, rig.step, { otherData });
					encoder->endEncoding();
				});
			});
			report(between(recorded, compute, compute) == 0, "serial: dispatches over different buffers do not wait for each other");

			recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Concurrent);
					for (int i = 0; i < 3; ++i) {
						rig.dispatch(*encoder, rig.step, { data });
					}
					encoder->endEncoding();
				});
			});
			report(between(recorded, compute, compute) == 0, "concurrent: dispatches over one buffer are not ordered");

			recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Concurrent);
					rig.dispatch(*encoder, rig.step, { data });
					encoder->memoryBarrier(Indium::BarrierScope::Buffers);
					rig.dispatch(*encoder, rig.step, { data });
					encoder->endEncoding();
				});
			});
			report(between(recorded, compute, compute) == 1, "concurrent: a memory barrier orders the dispatches before it before the ones after it");

			// what a concurrent encoder wrote is waited for by the encoders after it
			auto copy = rig.buffer();
			auto fresh = rig.counting();
			recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Concurrent);
					rig.dispatch(*encoder, rig.step, { fresh });
					rig.dispatch(*encoder, rig.step, { fresh });
					encoder->endEncoding();
					copyBuffer(cb, fresh, copy);
				});
			});
			report(between(recorded, compute, transfer) == 1, "concurrent: the encoder after it waits for what it wrote");
		}

		// --- resources declared with useResource and useHeap ----------------------------------------
		{
			auto written = rig.counting();
			auto bound = rig.counting();
			auto recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Serial);
					rig.dispatch(*encoder, rig.step, { written });
					rig.dispatch(*encoder, rig.step, { bound });
					encoder->endEncoding();
				});
			});
			report(between(recorded, compute, compute) == 0, "a dispatch that does not name a buffer does not wait for the one that wrote it");

			recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Serial);
					rig.dispatch(*encoder, rig.step, { written });
					encoder->useResource(written, Indium::ResourceUsage::Read);
					rig.dispatch(*encoder, rig.step, { bound });
					encoder->endEncoding();
				});
			});
			report(between(recorded, compute, compute) == 1, "useResource: a dispatch that may reach a buffer through memory waits for the dispatch that wrote it");

			Indium::HeapDescriptor heapDescriptor;
			heapDescriptor.size = 1 << 16;
			heapDescriptor.storageMode = Indium::StorageMode::Shared;
			heapDescriptor.hazardTrackingMode = Indium::HazardTrackingMode::HazardTrackingModeTracked;
			auto trackedHeap = device->newHeap(heapDescriptor);
			heapDescriptor.hazardTrackingMode = Indium::HazardTrackingMode::HazardTrackingModeUntracked;
			auto untrackedHeap = device->newHeap(heapDescriptor);
			report(trackedHeap && untrackedHeap, "heaps of both modes are made");
			if (trackedHeap && untrackedHeap) {
				report(trackedHeap->newBuffer(bytes, Indium::ResourceOptions::StorageModeShared)->hazardTrackingMode() == Indium::HazardTrackingMode::HazardTrackingModeTracked,
					"a buffer of a tracked heap is tracked");
				report(untrackedHeap->newBuffer(bytes, Indium::ResourceOptions::StorageModeShared)->hazardTrackingMode() == Indium::HazardTrackingMode::HazardTrackingModeUntracked,
					"a buffer of an untracked heap is untracked");

				auto inTracked = trackedHeap->newBuffer(bytes, Indium::ResourceOptions::StorageModeShared);
				auto inUntracked = untrackedHeap->newBuffer(bytes, Indium::ResourceOptions::StorageModeShared);
				for (auto& heapBuffer: { inTracked, inUntracked }) {
					auto words = static_cast<uint32_t*>(heapBuffer->contents());
					for (size_t i = 0; i < elements; ++i) {
						words[i] = static_cast<uint32_t>(i);
					}
				}
				recorded = observe([&]() {
					rig.run([&](Indium::CommandBuffer& cb) {
						auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Serial);
						rig.dispatch(*encoder, rig.step, { inTracked });
						encoder->useHeap(trackedHeap);
						rig.dispatch(*encoder, rig.step, { bound });
						encoder->endEncoding();
					});
				});
				report(between(recorded, compute, compute) == 1, "useHeap: a tracked heap's buffer is ordered for the dispatches after the declaration");

				recorded = observe([&]() {
					rig.run([&](Indium::CommandBuffer& cb) {
						auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Serial);
						rig.dispatch(*encoder, rig.step, { inUntracked });
						encoder->useHeap(untrackedHeap);
						rig.dispatch(*encoder, rig.step, { bound });
						encoder->endEncoding();
					});
				});
				// the dispatches of a serial encoder that reach resources nothing tracks are ordered, whichever memory
				// they share, so the second one waits; it is the encoder's order, not a tracked hazard
				report(between(recorded, compute, compute) <= 1, "useHeap: an untracked heap adds no dependency of its own");
			}
		}

		// --- untracked resources ---------------------------------------------------------------------
		{
			auto written = rig.counting(false);
			auto copy = rig.buffer(bytes, false);
			auto recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder();
					rig.dispatch(*encoder, rig.step, { written });
					encoder->endEncoding();
					copyBuffer(cb, written, copy);
				});
			});
			report(between(recorded, compute, transfer) == 0 && imports(recorded) == 0, "untracked: an encoder that reads what another wrote waits for nothing");

			auto fence = device->newFence();
			recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder();
					rig.dispatch(*encoder, rig.step, { written });
					encoder->updateFence(fence);
					encoder->endEncoding();
					auto blit = cb.blitCommandEncoder();
					blit->waitForFence(fence);
					blit->copy(written, 0, copy, 0, bytes);
					blit->endEncoding();
				});
			});
			report(fullBarriers(recorded) == 1, "untracked: waiting for a fence orders everything before it");
			report(holdsStepped(copy, 2), "untracked: what the fence's encoder wrote is in the copy");

			// a render pass waits for a fence in front of its pass, not inside it
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
			descriptor.usage = Indium::TextureUsage::RenderTarget;
			Indium::RenderPassDescriptor pass {};
			pass.colorAttachments.emplace_back();
			pass.colorAttachments.back().texture = device->newTexture(descriptor);
			pass.colorAttachments.back().loadAction = Indium::LoadAction::Clear;
			rig.run([&](Indium::CommandBuffer& cb) {
				forgetBarriers();
				auto render = cb.renderCommandEncoder(pass);
				render->waitForFence(fence, Indium::RenderStages::Vertex);
				auto duringPass = recordedBarriers();
				render->endEncoding();
				auto afterPass = recordedBarriers();
				report(fullBarriers(duringPass) == 0 && fullBarriers(afterPass) == 1, "untracked: a render pass that waits for a fence has the barrier in front of it, none inside");
			});
		}

		// --- texture views, buffer-backed textures --------------------------------------------------
		{
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, targetSize, targetSize, false);
			descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead | Indium::TextureUsage::PixelFormatView;
			auto target = device->newTexture(descriptor);
			auto otherTarget = device->newTexture(descriptor);
			auto view = target->newTextureView(Indium::PixelFormat::RGBA8Unorm);
			report(view != nullptr, "a view of the target is made");

			Indium::RenderPassDescriptor pass {};
			pass.colorAttachments.emplace_back();
			pass.colorAttachments[0].texture = target;
			pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
			pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
			pass.colorAttachments[0].clearColor = Indium::ClearColor(1, 0, 0, 1);

			auto texelBuffer = rig.buffer(targetSize * targetSize * 4);
			auto otherTexelBuffer = rig.buffer(targetSize * targetSize * 4);
			auto readTexture = [&](Indium::CommandBuffer& cb, const std::shared_ptr<Indium::Texture>& texture, const std::shared_ptr<Indium::Buffer>& destination) {
				auto blit = cb.blitCommandEncoder();
				blit->copy(texture, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { targetSize, targetSize, 1 }, destination, 0, targetSize * 4, targetSize * targetSize * 4);
				blit->endEncoding();
			};

			if (view) {
				auto recorded = observe([&]() {
					rig.run([&](Indium::CommandBuffer& cb) {
						cb.renderCommandEncoder(pass)->endEncoding();
						readTexture(cb, view, texelBuffer);
					});
				});
				report(between(recorded, colorOutput, transfer) == 1, "a view is its texture: a copy from the view waits for the pass that rendered the texture");
				report(static_cast<const uint32_t*>(texelBuffer->contents())[9] == red, "a view is its texture: the copy holds the cleared color");

				recorded = observe([&]() {
					rig.run([&](Indium::CommandBuffer& cb) {
						cb.renderCommandEncoder(pass)->endEncoding();
						readTexture(cb, otherTarget, otherTexelBuffer);
					});
				});
				report(between(recorded, colorOutput, transfer) == 0, "a copy from another texture does not wait for the pass");

				recorded = observe([&]() {
					rig.run([&](Indium::CommandBuffer& cb) {
						cb.renderCommandEncoder(pass)->endEncoding();
						pass.colorAttachments[0].loadAction = Indium::LoadAction::Load;
						cb.renderCommandEncoder(pass)->endEncoding();
						pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
					});
				});
				report(between(recorded, colorOutput, colorOutput) == 1, "a pass that loads what the pass before it rendered waits for it");
			}

			// a texture over the memory of a buffer is that memory
			Indium::TextureDescriptor over = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 64, 4, false);
			over.resourceOptions = Indium::ResourceOptions::StorageModeShared;
			over.usage = Indium::TextureUsage::ShaderRead | Indium::TextureUsage::ShaderWrite;
			auto backing = rig.buffer(64 * 4 * 4);
			auto overTexture = backing->newTexture(over, 0, 64 * 4);
			if (overTexture) {
				auto copyOut = rig.buffer(64 * 4 * 4);
				auto wordsPerBacking = 64 * 4;
				std::memset(backing->contents(), 0, backing->length());
				auto recorded = observe([&]() {
					rig.run([&](Indium::CommandBuffer& cb) {
						auto blit = cb.blitCommandEncoder();
						blit->fillBuffer(backing, Indium::Range<size_t> { 0, backing->length() }, 0x5A);
						blit->endEncoding();
						auto read = cb.blitCommandEncoder();
						read->copy(overTexture, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { 64, 4, 1 }, copyOut, 0, 64 * 4, 64 * 4 * 4);
						read->endEncoding();
					});
				});
				report(between(recorded, transfer, transfer) >= 1, "a texture over a buffer's memory waits for what wrote the buffer");
				report(static_cast<const uint8_t*>(copyOut->contents())[wordsPerBacking] == 0x5A, "a texture over a buffer's memory shows what was written to the buffer");
			} else {
				std::printf("SKIP the device cannot make a texture over a buffer at this pitch\n");
			}
		}

		// --- an indirect draw reads what a dispatch wrote, a pass is read by a copy -----------------
		{
			auto args = rig.buffer(64);
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, targetSize, targetSize, false);
			descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
			auto target = device->newTexture(descriptor);
			auto pixels = rig.buffer(targetSize * targetSize * 4);
			std::memset(args->contents(), 0, 64);
			auto recorded = observe([&]() {
				rig.run([&](Indium::CommandBuffer& cb) {
					auto fill = cb.computeCommandEncoder();
					fill->setComputePipelineState(rig.fillDrawArguments);
					fill->setBuffer(args, 0, 0);
					fill->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
					fill->endEncoding();

					Indium::RenderPassDescriptor pass {};
					pass.colorAttachments.emplace_back();
					pass.colorAttachments[0].texture = target;
					pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
					pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
					pass.colorAttachments[0].clearColor = Indium::ClearColor(0, 0, 0, 0);
					auto render = cb.renderCommandEncoder(pass);
					render->setRenderPipelineState(rig.redTriangle);
					render->drawPrimitives(Indium::PrimitiveType::Triangle, args, 0);
					render->endEncoding();

					auto blit = cb.blitCommandEncoder();
					blit->copy(target, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { targetSize, targetSize, 1 }, pixels, 0, targetSize * 4, targetSize * targetSize * 4);
					blit->endEncoding();
				});
			});
			report(between(recorded, compute, drawIndirect) == 1, "compute to render: an indirect draw waits for the dispatch that wrote its arguments");
			report(between(recorded, colorOutput, transfer) == 1, "render to blit: the copy waits for the pass");
			size_t redPixels = 0;
			for (size_t i = 0; i < targetSize * targetSize; ++i) {
				redPixels += static_cast<const uint32_t*>(pixels->contents())[i] == red;
			}
			report(redPixels == targetSize * targetSize, "compute to render to blit: every pixel is red");
		}

		// ---------------------------------------------------------------------------------------------
		// the results
		// ---------------------------------------------------------------------------------------------

		{
			auto data = rig.counting();
			rig.run([&](Indium::CommandBuffer& cb) {
				auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Serial);
				for (int i = 0; i < 64; ++i) {
					rig.dispatch(*encoder, rig.step, { data });
				}
				encoder->endEncoding();
			});
			report(holdsStepped(data, 64), "result: 64 dependent dispatches of a serial encoder");

			data = rig.counting();
			rig.run([&](Indium::CommandBuffer& cb) {
				auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Concurrent);
				for (int i = 0; i < 16; ++i) {
					rig.dispatch(*encoder, rig.step, { data });
					encoder->memoryBarrier(Indium::BarrierScope::Buffers);
				}
				encoder->endEncoding();
			});
			report(holdsStepped(data, 16), "result: 16 dependent dispatches of a concurrent encoder with a memory barrier after each");

			data = rig.counting();
			rig.run([&](Indium::CommandBuffer& cb) {
				for (int i = 0; i < 32; ++i) {
					auto encoder = cb.computeCommandEncoder();
					rig.dispatch(*encoder, rig.step, { data });
					encoder->endEncoding();
				}
			});
			report(holdsStepped(data, 32), "result: 32 dependent encoders");

			// compute, copy, compute, copy over several buffers
			auto a = rig.counting();
			auto b = rig.buffer();
			auto c = rig.buffer();
			rig.run([&](Indium::CommandBuffer& cb) {
				for (int round = 0; round < 8; ++round) {
					auto encoder = cb.computeCommandEncoder();
					rig.dispatch(*encoder, rig.addOne, { a, b });
					encoder->endEncoding();
					copyBuffer(cb, b, c);
					auto again = cb.computeCommandEncoder();
					rig.dispatch(*again, rig.addOne, { c, a });
					again->endEncoding();
				}
			});
			bool chained = true;
			for (size_t i = 0; i < elements; ++i) {
				chained = chained && static_cast<const uint32_t*>(a->contents())[i] == i + 16;
			}
			report(chained, "result: compute, copy, compute through three buffers, eight times");

			// independent chains are each right
			auto left = rig.counting();
			auto right = rig.counting();
			rig.run([&](Indium::CommandBuffer& cb) {
				for (int i = 0; i < 16; ++i) {
					auto first = cb.computeCommandEncoder();
					rig.dispatch(*first, rig.step, { left });
					first->endEncoding();
					auto second = cb.computeCommandEncoder();
					rig.dispatch(*second, rig.step, { right });
					rig.dispatch(*second, rig.step, { right });
					second->endEncoding();
				}
			});
			report(holdsStepped(left, 16) && holdsStepped(right, 32), "result: two independent chains in interleaved encoders");

			// command buffers committed one after the other without waiting
			data = rig.counting();
			std::vector<std::shared_ptr<Indium::CommandBuffer>> pending;
			for (int i = 0; i < 12; ++i) {
				auto commandBuffer = rig.queue->commandBuffer();
				auto encoder = commandBuffer->computeCommandEncoder();
				rig.dispatch(*encoder, rig.step, { data });
				encoder->endEncoding();
				commandBuffer->commit();
				pending.push_back(commandBuffer);
			}
			pending.back()->waitUntilCompleted();
			report(holdsStepped(data, 12), "result: 12 dependent command buffers committed together");

			// tracked heap buffers
			Indium::HeapDescriptor heapDescriptor;
			heapDescriptor.size = 1 << 16;
			heapDescriptor.storageMode = Indium::StorageMode::Shared;
			heapDescriptor.hazardTrackingMode = Indium::HazardTrackingMode::HazardTrackingModeTracked;
			if (auto heap = device->newHeap(heapDescriptor)) {
				auto inHeap = heap->newBuffer(bytes, Indium::ResourceOptions::StorageModeShared);
				for (size_t i = 0; i < elements; ++i) {
					static_cast<uint32_t*>(inHeap->contents())[i] = static_cast<uint32_t>(i);
				}
				rig.run([&](Indium::CommandBuffer& cb) {
					for (int i = 0; i < 8; ++i) {
						auto encoder = cb.computeCommandEncoder();
						rig.dispatch(*encoder, rig.step, { inHeap });
						encoder->endEncoding();
					}
				});
				report(holdsStepped(inHeap, 8), "result: 8 dependent encoders over a tracked heap's buffer");
			}

			// untracked buffers ordered by a fence, and by an event between command buffers
			auto fence = device->newFence();
			auto untracked = rig.counting(false);
			rig.run([&](Indium::CommandBuffer& cb) {
				for (int i = 0; i < 8; ++i) {
					auto first = cb.computeCommandEncoder();
					first->waitForFence(fence);
					rig.dispatch(*first, rig.step, { untracked });
					first->updateFence(fence);
					first->endEncoding();
				}
			});
			report(holdsStepped(untracked, 8), "result: 8 encoders over an untracked buffer, ordered by a fence");

			auto event = device->newEvent();
			untracked = rig.counting(false);
			auto before = rig.queue->commandBuffer();
			{
				auto encoder = before->computeCommandEncoder();
				rig.dispatch(*encoder, rig.step, { untracked });
				rig.dispatch(*encoder, rig.step, { untracked });
				encoder->endEncoding();
				before->encodeSignalEvent(event, 1);
			}
			auto after = rig.queue->commandBuffer();
			after->encodeWaitForEvent(event, 1);
			{
				auto encoder = after->computeCommandEncoder();
				rig.dispatch(*encoder, rig.step, { untracked });
				encoder->endEncoding();
			}
			before->commit();
			after->commit();
			after->waitUntilCompleted();
			report(holdsStepped(untracked, 3), "result: untracked work ordered by an event between command buffers");
		}

		// ---------------------------------------------------------------------------------------------
		// with a texture kept equal to a buffer by copies around the encoders, which is the last thing here because
		// the device remembers it: every encoder is ordered after everything before it
		// ---------------------------------------------------------------------------------------------
		{
			// 48 bytes is a row pitch that is not a multiple of the unit the device's images use for it
			auto shadowBuffer = rig.buffer(48 * 4);
			auto shadowDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 5, 4, false);
			shadowDescriptor.resourceOptions = Indium::ResourceOptions::StorageModeShared;
			auto shadow = shadowBuffer->newTexture(shadowDescriptor, 0, 48);
			if (!shadow) {
				std::printf("SKIP the device cannot make a texture over a buffer at this pitch\n");
			} else {
				auto data = rig.counting();
				rig.run([&](Indium::CommandBuffer& cb) {
					auto encoder = cb.computeCommandEncoder(Indium::DispatchType::Serial);
					for (int i = 0; i < 32; ++i) {
						rig.dispatch(*encoder, rig.step, { data });
					}
					encoder->endEncoding();
				});
				report(holdsStepped(data, 32), "shadow: 32 dependent dispatches of a serial encoder");

				auto a = rig.counting();
				auto b = rig.buffer();
				auto c = rig.buffer();
				rig.run([&](Indium::CommandBuffer& cb) {
					for (int round = 0; round < 4; ++round) {
						auto encoder = cb.computeCommandEncoder();
						rig.dispatch(*encoder, rig.addOne, { a, b });
						encoder->endEncoding();
						copyBuffer(cb, b, c);
						auto again = cb.computeCommandEncoder();
						rig.dispatch(*again, rig.addOne, { c, a });
						again->endEncoding();
					}
				});
				bool chained = true;
				for (size_t i = 0; i < elements; ++i) {
					chained = chained && static_cast<const uint32_t*>(a->contents())[i] == i + 8;
				}
				report(chained, "shadow: compute, copy, compute through three buffers, four times");
			}
		}

		Indium::DynamicVK::vkCmdPipelineBarrier2.pointer = reinterpret_cast<void*>(originalBarrier2);

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
