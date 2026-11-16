// Copyright (C) 2026 Lubos Dolezel
//
// Events, shared events and fences (see README.md). The compute module is the one of test/indirect-draw-dispatch;
// its `mark` kernel stores gid + 1 in every word it covers, so a buffer that holds 1..8 says the kernel ran.
// Prints SKIP and exits 0 without a Vulkan device.

#include "compute-spv.h"

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr size_t noIndex = SIZE_MAX;
constexpr size_t words = 8;
constexpr size_t bufferSize = words * sizeof(uint32_t);

bool eventually(const std::function<bool()>& condition, int timeoutMilliseconds = 10000) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
	while (!condition()) {
		if (std::chrono::steady_clock::now() > deadline) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return true;
}

void settle() {
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

// the barriers recorded since the last reset that make every earlier write visible to every later access
PFN_vkCmdPipelineBarrier originalBarrier;
std::atomic<unsigned> globalBarriers { 0 };

void VKAPI_CALL recordBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStages, VkPipelineStageFlags dstStages, VkDependencyFlags flags,
	uint32_t memoryBarrierCount, const VkMemoryBarrier* memoryBarriers,
	uint32_t bufferBarrierCount, const VkBufferMemoryBarrier* bufferBarriers,
	uint32_t imageBarrierCount, const VkImageMemoryBarrier* imageBarriers)
{
	for (uint32_t i = 0; i < memoryBarrierCount; ++i) {
		if ((memoryBarriers[i].srcAccessMask & VK_ACCESS_MEMORY_WRITE_BIT) && (memoryBarriers[i].dstAccessMask & VK_ACCESS_MEMORY_READ_BIT) &&
			(srcStages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) && (dstStages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)) {
			++globalBarriers;
		}
	}
	originalBarrier(commandBuffer, srcStages, dstStages, flags, memoryBarrierCount, memoryBarriers, bufferBarrierCount, bufferBarriers, imageBarrierCount, imageBarriers);
}

template<class Exception, class Function>
bool throwsException(Function&& function) {
	try {
		function();
	} catch (const Exception&) {
		return true;
	} catch (...) {
		return false;
	}
	return false;
}

struct Rig {
	std::shared_ptr<Indium::Device> device;
	std::shared_ptr<Indium::CommandQueue> queueA;
	std::shared_ptr<Indium::CommandQueue> queueB;
	std::shared_ptr<Indium::ComputePipelineState> mark;

	std::shared_ptr<Indium::Buffer> newBuffer(uint8_t fill) {
		auto buffer = device->newBuffer(bufferSize, Indium::ResourceOptions::StorageModeShared);
		std::memset(buffer->contents(), fill, bufferSize);
		return buffer;
	}

	void encodeMark(const std::shared_ptr<Indium::CommandBuffer>& commandBuffer, const std::shared_ptr<Indium::Buffer>& out) {
		auto encoder = commandBuffer->computeCommandEncoder();
		encoder->setComputePipelineState(mark);
		encoder->setBuffer(out, 0, 0);
		encoder->dispatchThreadgroups(Indium::Size { 4, 1, 1 }, Indium::Size { 2, 1, 1 });
		encoder->endEncoding();
	}

	void encodeCopy(const std::shared_ptr<Indium::CommandBuffer>& commandBuffer, const std::shared_ptr<Indium::Buffer>& source, const std::shared_ptr<Indium::Buffer>& destination) {
		auto encoder = commandBuffer->blitCommandEncoder();
		encoder->copy(source, 0, destination, 0, bufferSize);
		encoder->endEncoding();
	}
};

bool isMarked(const std::shared_ptr<Indium::Buffer>& buffer) {
	auto contents = static_cast<const uint32_t*>(buffer->contents());
	for (size_t i = 0; i < words; ++i) {
		if (contents[i] != i + 1) {
			return false;
		}
	}
	return true;
}

bool isFilled(const std::shared_ptr<Indium::Buffer>& buffer, uint8_t fill) {
	auto contents = static_cast<const uint8_t*>(buffer->contents());
	for (size_t i = 0; i < bufferSize; ++i) {
		if (contents[i] != fill) {
			return false;
		}
	}
	return true;
}

// commits the command buffer and returns a flag that says when it has completed
std::shared_ptr<std::atomic<bool>> commitTracked(const std::shared_ptr<Indium::CommandBuffer>& commandBuffer) {
	auto done = std::make_shared<std::atomic<bool>>(false);
	commandBuffer->addCompletedHandler([done](std::shared_ptr<Indium::CommandBuffer>) {
		done->store(true);
	});
	commandBuffer->commit();
	return done;
}

}

int main() {
	Indium::init(nullptr, 0, false);

	{
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}
		std::printf("device: %s\n", device->name().c_str());

		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		Indium::LibraryReflection reflection;
		auto& markFunction = reflection.functions["mark"];
		markFunction.functionType = Indium::FunctionType::Kernel;
		markFunction.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
		std::string error;
		auto library = device->newLibrary(compute_spv, compute_spv_len, reflection, &error);
		report(library != nullptr, "the compute module loads");
		if (!library) {
			std::printf("%s\n", error.c_str());
			keepPollingDevice = false;
			device->wakeupEventLoop();
			devicePollingThread.join();
			Indium::finit();
			return 1;
		}

		Rig rig;
		rig.device = device;
		rig.queueA = device->newCommandQueue();
		rig.queueB = device->newCommandQueue();
		rig.mark = device->newComputePipelineState(library->newFunction("mark"));

		// --- objects ---------------------------------------------------------------------------------
		{
			auto event = device->newEvent();
			auto shared = device->newSharedEvent();
			auto fence = device->newFence();
			report(event && shared && fence, "an event, a shared event and a fence can be made");
			report(event->device() == device && shared->device() == device && fence->device() == device, "they report their device");
			report(shared->signaledValue() == 0, "a shared event starts at 0");
		}

		// --- the consumer is committed before the producer, on another queue ---------------------------
		{
			auto event = device->newEvent();
			auto out = rig.newBuffer(0xFF);
			auto destination = rig.newBuffer(0xFF);

			auto consumer = rig.queueB->commandBuffer();
			consumer->encodeWaitForEvent(event, 1);
			rig.encodeCopy(consumer, out, destination);
			auto consumerDone = commitTracked(consumer);
			settle();
			report(!*consumerDone && isFilled(destination, 0xFF), "a command buffer that waits for an event value does not run before it");

			auto producer = rig.queueA->commandBuffer();
			rig.encodeMark(producer, out);
			producer->encodeSignalEvent(event, 1);
			commitTracked(producer);
			report(eventually([&]() { return consumerDone->load(); }), "the waiting command buffer completes once another queue signals the value");
			report(isMarked(destination), "it saw what the signaling command buffer wrote before the signal");
		}

		// --- signal and wait inside one command buffer ----------------------------------------------------
		{
			auto event = device->newEvent();
			auto out = rig.newBuffer(0xFF);
			auto destination = rig.newBuffer(0xFF);

			auto commandBuffer = rig.queueA->commandBuffer();
			rig.encodeMark(commandBuffer, out);
			commandBuffer->encodeSignalEvent(event, 1);
			commandBuffer->encodeWaitForEvent(event, 1);
			rig.encodeCopy(commandBuffer, out, destination);
			auto done = commitTracked(commandBuffer);
			report(eventually([&]() { return done->load(); }), "a command buffer can signal an event and wait for the value later on");
			report(isMarked(destination), "the work after the wait ran after the work before the signal");
		}

		// --- a wait on a value that is already there ---------------------------------------------------
		{
			auto shared = device->newSharedEvent();
			shared->setSignaledValue(3);
			auto out = rig.newBuffer(0xFF);
			auto destination = rig.newBuffer(0xFF);

			auto commandBuffer = rig.queueA->commandBuffer();
			commandBuffer->encodeWaitForEvent(shared, 2);
			commandBuffer->encodeWaitForEvent(shared, 3);
			rig.encodeMark(commandBuffer, out);
			rig.encodeCopy(commandBuffer, out, destination);
			auto done = commitTracked(commandBuffer);
			report(eventually([&]() { return done->load(); }) && isMarked(destination), "a wait on a value the event has already reached holds nothing up");
		}

		// --- the host signals ---------------------------------------------------------------------------
		{
			auto shared = device->newSharedEvent();
			auto out = rig.newBuffer(0xFF);
			auto other = rig.newBuffer(0xFF);

			auto blocked = rig.queueA->commandBuffer();
			blocked->encodeWaitForEvent(shared, 5);
			rig.encodeMark(blocked, out);
			auto blockedDone = commitTracked(blocked);

			auto behind = rig.queueA->commandBuffer();
			rig.encodeMark(behind, other);
			auto behindDone = commitTracked(behind);

			auto elsewhere = rig.queueB->commandBuffer();
			auto elsewhereOut = rig.newBuffer(0xFF);
			rig.encodeMark(elsewhere, elsewhereOut);
			auto elsewhereDone = commitTracked(elsewhere);

			report(eventually([&]() { return elsewhereDone->load(); }) && isMarked(elsewhereOut), "a command buffer on another queue runs while one waits for an event");
			settle();
			report(!*blockedDone && isFilled(out, 0xFF), "a command buffer that waits for an event the host has not signaled does not run");
			report(!*behindDone && isFilled(other, 0xFF), "a command buffer committed after it on the same queue stays behind it");

			shared->setSignaledValue(4);
			settle();
			report(!*blockedDone, "a smaller value does not release it");

			shared->setSignaledValue(5);
			report(eventually([&]() { return blockedDone->load() && behindDone->load(); }), "the host reaching the value releases it and the command buffer behind it");
			report(isMarked(out) && isMarked(other), "both ran");
		}

		// --- a chain of command buffers across two queues, committed out of order -------------------------------
		{
			constexpr uint64_t steps = 40;
			auto event = device->newEvent();
			// step i copies byte i - 1 of the chain to byte i, so every byte ends up 1 only if the steps ran in turn
			auto chain = device->newBuffer(steps + 1, Indium::ResourceOptions::StorageModeShared);
			std::memset(chain->contents(), 0, steps + 1);
			static_cast<uint8_t*>(chain->contents())[0] = 1;
			std::vector<std::shared_ptr<std::atomic<bool>>> flags;

			// step i waits for i - 1 and signals i. Each queue holds its steps in ascending order, so the
			// steps of one queue are committed first and they all wait for the other queue's.
			for (uint64_t parity : { 0, 1 }) {
				auto& queue = parity == 0 ? rig.queueB : rig.queueA;
				for (uint64_t step = 1 + (parity == 0 ? 1 : 0); step <= steps; step += 2) {
					auto commandBuffer = queue->commandBuffer();
					if (step > 1) {
						commandBuffer->encodeWaitForEvent(event, step - 1);
					}
					auto blit = commandBuffer->blitCommandEncoder();
					blit->copy(chain, step - 1, chain, step, 1);
					blit->endEncoding();
					commandBuffer->encodeSignalEvent(event, step);
					flags.push_back(commitTracked(commandBuffer));
				}
				if (parity == 0) {
					settle();
				}
			}
			report(eventually([&]() {
				for (auto& flag : flags) {
					if (!flag->load()) {
						return false;
					}
				}
				return true;
			}), "forty command buffers chained by one event across two queues all complete");
			bool inTurn = true;
			for (uint64_t i = 0; i <= steps; ++i) {
				inTurn = inTurn && static_cast<uint8_t*>(chain->contents())[i] == 1;
			}
			report(inTurn, "and their work ran in the order of the values");
		}

		// --- values only go up --------------------------------------------------------------------------
		{
			auto shared = device->newSharedEvent();
			shared->setSignaledValue(6);
			shared->setSignaledValue(2);
			report(shared->signaledValue() == 6, "the host cannot lower the value");
			shared->setSignaledValue(6);
			report(shared->signaledValue() == 6, "the host setting the current value changes nothing");

			// Vulkan limits how far a timeline semaphore may move in one step on some devices; Metal does not
			auto big = device->newSharedEvent();
			big->setSignaledValue(1ull << 40);
			report(big->signaledValue() == 1ull << 40, "the host can raise the value by 2^40");
			big->setSignaledValue(UINT64_MAX);
			report(big->signaledValue() == UINT64_MAX, "and to the largest value");

			auto commandBuffer = rig.queueA->commandBuffer();
			commandBuffer->encodeSignalEvent(shared, 10);
			auto first = commitTracked(commandBuffer);
			report(eventually([&]() { return first->load(); }) && shared->signaledValue() == 10, "a command buffer's signal sets the value");

			auto lower = rig.queueA->commandBuffer();
			lower->encodeSignalEvent(shared, 4);
			lower->encodeSignalEvent(shared, 10);
			auto lowerDone = commitTracked(lower);
			report(eventually([&]() { return lowerDone->load(); }) && shared->signaledValue() == 10, "a signal that does not raise the value changes nothing");

			auto after = rig.queueA->commandBuffer();
			after->encodeSignalEvent(shared, 11);
			auto afterDone = commitTracked(after);
			report(eventually([&]() { return afterDone->load(); }) && shared->signaledValue() == 11, "the event still works after such signals");
		}

		// --- listeners -------------------------------------------------------------------------------------
		{
			auto shared = device->newSharedEvent();
			std::mutex mutex;
			std::vector<std::pair<int, uint64_t>> calls;
			auto record = [&](int tag) {
				return [&, tag]() {
					std::scoped_lock lock(mutex);
					calls.emplace_back(tag, shared->signaledValue());
				};
			};
			auto count = [&]() { std::scoped_lock lock(mutex); return calls.size(); };

			shared->notify(2, record(1));
			shared->notify(1, record(2));
			shared->notify(2, record(3));
			settle();
			report(count() == 0, "a listener is not called before its value");

			shared->setSignaledValue(1);
			report(eventually([&]() { return count() == 1; }), "it is called when the value is reached");
			{
				std::scoped_lock lock(mutex);
				report(calls[0].first == 2 && calls[0].second >= 1, "only the listener of the reached value is called");
			}

			shared->setSignaledValue(2);
			report(eventually([&]() { return count() == 3; }), "the others are called when theirs is reached");
			{
				std::scoped_lock lock(mutex);
				report(calls[1].first == 1 && calls[2].first == 3 && calls[1].second >= 2 && calls[2].second >= 2, "in the order they were registered");
			}

			shared->notify(1, record(4));
			report(eventually([&]() { return count() == 4; }), "a listener of a value that is already there is called");
			settle();
			report(count() == 4, "and each listener is called once");

			shared->notify(20, record(5));
			auto commandBuffer = rig.queueA->commandBuffer();
			commandBuffer->encodeSignalEvent(shared, 20);
			commandBuffer->commit();
			report(eventually([&]() { return count() == 5; }), "a command buffer's signal calls the listeners");
		}

		// --- waiting on the host --------------------------------------------------------------------------
		{
			auto shared = device->newSharedEvent();
			report(shared->waitUntilSignaledValue(0, 0), "waiting for 0 does not wait");
			auto start = std::chrono::steady_clock::now();
			bool reached = shared->waitUntilSignaledValue(10, 60);
			auto elapsed = std::chrono::steady_clock::now() - start;
			report(!reached && elapsed >= std::chrono::milliseconds(50), "waiting for a value that is not reached returns false after the timeout");

			std::thread signaler([&]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(80));
				shared->setSignaledValue(10);
			});
			report(shared->waitUntilSignaledValue(10, 10000), "waiting returns true when another thread reaches the value");
			signaler.join();
			report(shared->waitUntilSignaledValue(9, 0), "waiting for a value that has been passed returns true at once");

			auto commandBuffer = rig.queueA->commandBuffer();
			commandBuffer->encodeSignalEvent(shared, 30);
			commandBuffer->commit();
			report(shared->waitUntilSignaledValue(30, 10000), "waiting returns true when a command buffer signals the value");
		}

		// --- handles ---------------------------------------------------------------------------------------
		{
			auto first = device->newSharedEvent();
			auto handle = first->newSharedEventHandle();
			auto copy = device->newSharedEvent(handle);
			report(handle && copy && copy != first && copy->device() == device, "a handle makes another object");
			first->setSignaledValue(7);
			report(copy->signaledValue() == 7, "it sees the value of the original");
			copy->setSignaledValue(9);
			report(first->signaledValue() == 9, "and the original sees what it sets");

			auto commandBuffer = rig.queueA->commandBuffer();
			commandBuffer->encodeSignalEvent(copy, 11);
			auto done = commitTracked(commandBuffer);
			report(eventually([&]() { return done->load(); }) && first->signaledValue() == 11, "a command buffer signaling the copy raises the original");

			first.reset();
			auto later = device->newSharedEvent(handle);
			report(later && later->signaledValue() == 11, "the handle keeps the event alive");
			report(device->newSharedEvent(std::shared_ptr<Indium::SharedEventHandle>()) == nullptr, "an empty handle makes no event");
		}

		// --- refusals ----------------------------------------------------------------------------------------
		{
			auto event = device->newEvent();
			auto commandBuffer = rig.queueA->commandBuffer();
			report(throwsException<std::invalid_argument>([&]() { commandBuffer->encodeSignalEvent(nullptr, 1); }), "signaling no event is refused");
			report(throwsException<std::invalid_argument>([&]() { commandBuffer->encodeWaitForEvent(nullptr, 1); }), "waiting for no event is refused");

			auto encoder = commandBuffer->computeCommandEncoder();
			report(throwsException<std::logic_error>([&]() { commandBuffer->encodeSignalEvent(event, 1); }), "an event operation is refused while an encoder is active");
			encoder->endEncoding();
			report(!throwsException<std::logic_error>([&]() { commandBuffer->encodeSignalEvent(event, 1); }), "and accepted once it has ended");
			commandBuffer->commit();
			report(throwsException<std::logic_error>([&]() { commandBuffer->encodeSignalEvent(event, 2); }), "an event operation is refused after the commit");
			commandBuffer->waitUntilCompleted();
		}

		// --- fences ------------------------------------------------------------------------------------------
		{
			auto fence = device->newFence();
			auto out = rig.newBuffer(0xFF);
			auto destination = rig.newBuffer(0xFF);

			auto commandBuffer = rig.queueA->commandBuffer();
			auto compute = commandBuffer->computeCommandEncoder();
			compute->setComputePipelineState(rig.mark);
			compute->setBuffer(out, 0, 0);
			compute->dispatchThreadgroups(Indium::Size { 4, 1, 1 }, Indium::Size { 2, 1, 1 });
			compute->updateFence(fence);
			compute->endEncoding();
			auto blit = commandBuffer->blitCommandEncoder();
			blit->waitForFence(fence);
			blit->copy(out, 0, destination, 0, bufferSize);
			blit->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
			report(isMarked(destination), "an encoder that waits for a fence sees what the encoder that updated it wrote");

			// the next command buffer of the queue waits for it as well
			auto again = rig.newBuffer(0xFF);
			auto second = rig.queueA->commandBuffer();
			auto producer = second->computeCommandEncoder();
			producer->setComputePipelineState(rig.mark);
			producer->setBuffer(again, 0, 0);
			producer->dispatchThreadgroups(Indium::Size { 4, 1, 1 }, Indium::Size { 2, 1, 1 });
			producer->updateFence(fence);
			producer->endEncoding();
			second->commit();
			auto third = rig.queueA->commandBuffer();
			auto copyOut = rig.newBuffer(0xFF);
			auto consumer = third->blitCommandEncoder();
			consumer->waitForFence(fence);
			consumer->copy(again, 0, copyOut, 0, bufferSize);
			consumer->endEncoding();
			third->commit();
			third->waitUntilCompleted();
			report(isMarked(copyOut), "a fence also orders encoders of different command buffers of a queue");

			// what each encoder records for a fence
			Indium::DynamicVK::vkCmdPipelineBarrier.resolve();
			originalBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(Indium::DynamicVK::vkCmdPipelineBarrier.pointer);
			Indium::DynamicVK::vkCmdPipelineBarrier.pointer = reinterpret_cast<void*>(recordBarrier);

			auto probe = rig.queueA->commandBuffer();
			globalBarriers = 0;
			auto updating = probe->computeCommandEncoder();
			updating->updateFence(fence);
			updating->endEncoding();
			report(globalBarriers == 1, "updating a fence in a compute encoder records nothing besides the barrier that ends it");

			globalBarriers = 0;
			auto waiting = probe->computeCommandEncoder();
			waiting->waitForFence(fence);
			waiting->endEncoding();
			report(globalBarriers == 2, "waiting for a fence in a compute encoder records a memory dependency");

			globalBarriers = 0;
			auto blitWaiting = probe->blitCommandEncoder();
			blitWaiting->waitForFence(fence);
			blitWaiting->endEncoding();
			report(globalBarriers == 2, "waiting for a fence in a blit encoder records a memory dependency");

			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
			descriptor.usage = Indium::TextureUsage::RenderTarget;
			Indium::RenderPassDescriptor pass {};
			pass.colorAttachments.emplace_back();
			pass.colorAttachments.back().texture = device->newTexture(descriptor);
			pass.colorAttachments.back().loadAction = Indium::LoadAction::Clear;
			globalBarriers = 0;
			auto render = probe->renderCommandEncoder(pass);
			render->waitForFence(fence, Indium::RenderStages::Vertex);
			render->updateFence(fence, Indium::RenderStages::Fragment);
			render->endEncoding();
			report(globalBarriers == 1, "a render encoder takes fence calls without a barrier inside its pass");

			probe->commit();
			probe->waitUntilCompleted();
			Indium::DynamicVK::vkCmdPipelineBarrier.pointer = reinterpret_cast<void*>(originalBarrier);

			auto cb = rig.queueA->commandBuffer();
			auto encoder = cb->computeCommandEncoder();
			report(throwsException<std::invalid_argument>([&]() { encoder->updateFence(nullptr); }), "updating no fence is refused");
			report(throwsException<std::invalid_argument>([&]() { encoder->waitForFence(nullptr); }), "waiting for no fence is refused");
			encoder->endEncoding();
			cb->commit();
			cb->waitUntilCompleted();
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
