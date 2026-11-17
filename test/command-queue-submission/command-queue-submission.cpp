// Copyright (C) 2026 Lubos Dolezel
//
// Submission, handlers and retirement of the command buffers of a queue (see README.md).

#include "compute-spv.h"

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace Indium;
namespace DynamicVK = Indium::DynamicVK;

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

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

std::shared_ptr<PrivateDevice> device;
std::shared_ptr<ComputePipelineState> pipeline;
std::thread::id pollerThread;

std::shared_ptr<Buffer> makeBuffer() {
	return device->newBuffer(64 * sizeof(uint32_t), ResourceOptions::StorageModeShared);
}

// a command buffer with one small dispatch over `buffer`
std::shared_ptr<CommandBuffer> makeCommands(const std::shared_ptr<CommandQueue>& queue, const std::shared_ptr<Buffer>& buffer) {
	auto commands = queue->commandBuffer();
	auto encoder = commands->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(buffer, 0, 0);
	encoder->dispatchThreadgroups(Size { 1, 1, 1 }, Size { 64, 1, 1 });
	encoder->endEncoding();
	return commands;
}

// Holds everything submitted to the Vulkan queue after it back until release().
class Gate {
public:
	Gate() {
		VkSemaphoreTypeCreateInfo type {};
		type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		VkSemaphoreCreateInfo create {};
		create.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		create.pNext = &type;
		DynamicVK::vkCreateSemaphore(device->device(), &create, nullptr, &_semaphore);
		VkFenceCreateInfo fenceInfo {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		DynamicVK::vkCreateFence(device->device(), &fenceInfo, nullptr, &_fence);

		VkSemaphoreSubmitInfo wait {};
		wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		wait.semaphore = _semaphore;
		wait.value = 1;
		wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		VkSubmitInfo2 submit {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit.waitSemaphoreInfoCount = 1;
		submit.pWaitSemaphoreInfos = &wait;
		std::scoped_lock lock(device->graphicsQueueMutex());
		DynamicVK::vkQueueSubmit2(device->graphicsQueue(), 1, &submit, _fence);
	}

	void release() {
		if (_released) {
			return;
		}
		_released = true;
		VkSemaphoreSignalInfo signal {};
		signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
		signal.semaphore = _semaphore;
		signal.value = 1;
		DynamicVK::vkSignalSemaphore(device->device(), &signal);
	}

	~Gate() {
		release();
		DynamicVK::vkWaitForFences(device->device(), 1, &_fence, VK_TRUE, UINT64_MAX);
		DynamicVK::vkDestroyFence(device->device(), _fence, nullptr);
		DynamicVK::vkDestroySemaphore(device->device(), _semaphore, nullptr);
	}

private:
	VkSemaphore _semaphore = VK_NULL_HANDLE;
	VkFence _fence = VK_NULL_HANDLE;
	bool _released = false;
};

// what the handlers of a set of command buffers did, in the order they did it
struct Log {
	std::mutex mutex;
	std::vector<std::pair<char, int>> events; // 'S' scheduled, 'C' completed
	std::set<std::thread::id> threads;

	void add(char kind, int id) {
		std::scoped_lock lock(mutex);
		events.emplace_back(kind, id);
		threads.insert(std::this_thread::get_id());
	}
	size_t count() {
		std::scoped_lock lock(mutex);
		return events.size();
	}
	std::vector<std::pair<char, int>> snapshot() {
		std::scoped_lock lock(mutex);
		return events;
	}
};

void addLogging(const std::shared_ptr<CommandBuffer>& commands, Log& log, int id) {
	commands->addScheduledHandler([&log, id](auto) { log.add('S', id); });
	commands->addCompletedHandler([&log, id](auto) { log.add('C', id); });
}

void commitReturnsWithoutWaiting() {
	auto queue = device->newCommandQueue();
	auto buffer = makeBuffer();
	Gate gate;

	std::mutex mutex;
	std::condition_variable condition;
	bool inHandler = false;
	bool release = false;
	auto commands = makeCommands(queue, buffer);
	commands->addScheduledHandler([&](auto) {
		std::unique_lock lock(mutex);
		inHandler = true;
		condition.notify_all();
		condition.wait(lock, [&]() { return release; });
	});

	auto start = std::chrono::steady_clock::now();
	commands->commit();
	report(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "commit returns while the GPU is held");
	{
		std::unique_lock lock(mutex);
		report(condition.wait_for(lock, std::chrono::seconds(10), [&]() { return inHandler; }), "the scheduled handler runs while the GPU is held");
	}

	// the handler blocks: the committing thread must be free to go on, and so must the queue's other work
	auto next = makeCommands(queue, buffer);
	std::atomic<bool> nextScheduled { false };
	next->addScheduledHandler([&](auto) { nextScheduled = true; });
	start = std::chrono::steady_clock::now();
	next->commit();
	report(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "commit returns while a scheduled handler is blocked");

	std::thread waiter([&]() { commands->waitUntilScheduled(); });
	settle();
	{
		std::scoped_lock lock(mutex);
		release = true;
		condition.notify_all();
	}
	waiter.join();
	report(true, "waitUntilScheduled returns once the handler has returned");
	report(eventually([&]() { return nextScheduled.load(); }), "the next command buffer is scheduled behind it");
	gate.release();
	commands->waitUntilCompleted();
	next->waitUntilCompleted();
}

void handlerOrder() {
	auto queue = device->newCommandQueue();
	constexpr int count = 200;
	std::vector<std::shared_ptr<Buffer>> buffers;
	std::vector<std::shared_ptr<CommandBuffer>> commands;
	Log log;
	{
		Gate gate;
		for (int i = 0; i < count; ++i) {
			buffers.push_back(makeBuffer());
			commands.push_back(makeCommands(queue, buffers.back()));
			addLogging(commands.back(), log, i);
			commands.back()->commit();
		}
		settle();
		gate.release();
	}
	commands.back()->waitUntilCompleted();
	report(eventually([&]() { return log.count() == 2 * count; }), "every handler is called once");

	auto events = log.snapshot();
	bool scheduledOrder = true;
	bool completedOrder = true;
	bool scheduledBeforeCompleted = true;
	int lastScheduled = -1;
	int lastCompleted = -1;
	std::vector<bool> scheduled(count, false);
	for (const auto& [kind, id] : events) {
		if (kind == 'S') {
			scheduledOrder &= id == lastScheduled + 1;
			lastScheduled = id;
			scheduled[id] = true;
		} else {
			completedOrder &= id == lastCompleted + 1;
			lastCompleted = id;
			scheduledBeforeCompleted &= scheduled[id];
		}
	}
	report(scheduledOrder, "scheduled handlers run in commit order");
	report(completedOrder, "completed handlers run in commit order");
	report(scheduledBeforeCompleted, "the scheduled handlers of a command buffer run before its completed handlers");
	report(log.threads.count(std::this_thread::get_id()) == 0 && log.threads.count(pollerThread) == 0, "handlers run on neither the committing nor the polling thread");
}

void manyThreads() {
	auto queue = device->newCommandQueue();
	constexpr int threadCount = 6;
	constexpr int perThread = 150;
	Log log;
	std::vector<std::thread> threads;
	std::mutex keep;
	std::vector<std::shared_ptr<CommandBuffer>> all;
	for (int t = 0; t < threadCount; ++t) {
		threads.emplace_back([&, t]() {
			auto buffer = makeBuffer();
			for (int i = 0; i < perThread; ++i) {
				auto commands = makeCommands(queue, buffer);
				addLogging(commands, log, t * 1000 + i);
				commands->commit();
				std::scoped_lock lock(keep);
				all.push_back(commands);
			}
		});
	}
	for (auto& thread : threads) {
		thread.join();
	}
	report(eventually([&]() { return log.count() == 2u * threadCount * perThread; }), "commits from many threads: every handler is called once");

	std::vector<int> lastScheduled(threadCount, -1);
	std::vector<int> lastCompleted(threadCount, -1);
	bool ordered = true;
	std::set<int> seen;
	for (const auto& [kind, id] : log.snapshot()) {
		int t = id / 1000, i = id % 1000;
		auto& last = kind == 'S' ? lastScheduled : lastCompleted;
		ordered &= i == last[t] + 1;
		last[t] = i;
		seen.insert(kind == 'S' ? id : -id - 1);
	}
	report(ordered, "commits from many threads: each thread's command buffers are handled in its commit order");
	report(seen.size() == 2u * threadCount * perThread, "commits from many threads: no handler is called twice");
}

void enqueueOrder() {
	auto queue = device->newCommandQueue();
	auto buffer = makeBuffer();
	Log log;
	auto first = makeCommands(queue, buffer);
	auto second = makeCommands(queue, buffer);
	auto third = makeCommands(queue, buffer);
	addLogging(first, log, 1);
	addLogging(second, log, 2);
	addLogging(third, log, 3);
	first->enqueue();
	second->enqueue();
	third->enqueue();
	third->commit();
	second->commit();
	settle();
	report(log.count() == 0, "a command buffer waits for the ones enqueued before it");
	first->commit();
	third->waitUntilCompleted();
	report(eventually([&]() { return log.count() == 6; }), "enqueued command buffers run once the first is committed");
	std::vector<int> scheduled, completed;
	for (const auto& [kind, id] : log.snapshot()) {
		(kind == 'S' ? scheduled : completed).push_back(id);
	}
	report((scheduled == std::vector<int> { 1, 2, 3 }) && (completed == std::vector<int> { 1, 2, 3 }), "enqueue order decides, not commit order");

	// committing enqueues, and enqueueing twice changes nothing
	Log other;
	auto a = makeCommands(queue, buffer);
	auto b = makeCommands(queue, buffer);
	addLogging(a, other, 1);
	addLogging(b, other, 2);
	b->enqueue();
	b->enqueue();
	a->commit();
	b->commit();
	b->waitUntilCompleted();
	report(eventually([&]() { return other.count() == 4; }), "a command buffer committed without enqueue goes behind the enqueued ones");
	std::vector<int> order;
	for (const auto& [kind, id] : other.snapshot()) {
		if (kind == 'C') {
			order.push_back(id);
		}
	}
	report(order == std::vector<int> { 2, 1 }, "committing enqueues a command buffer that has not been");

	// a command buffer that is enqueued and dropped must not hold the queue back
	std::atomic<bool> ran { false };
	{
		auto dropped = makeCommands(queue, buffer);
		dropped->enqueue();
		auto later = makeCommands(queue, buffer);
		later->addCompletedHandler([&](auto) { ran = true; });
		later->commit();
		settle();
		report(!ran, "a command buffer is held back by an enqueued one that is not committed yet");
		dropped.reset();
		report(eventually([&]() { return ran.load(); }), "an enqueued command buffer that is dropped gives up its place");
	}
}

void waitersAndReentrancy() {
	auto queue = device->newCommandQueue();
	auto buffer = makeBuffer();
	Gate gate;
	auto commands = makeCommands(queue, buffer);
	std::atomic<int> returned { 0 };
	std::thread other1([&]() { commands->waitUntilCompleted(); ++returned; });
	std::thread other2([&]() { commands->waitUntilCompleted(); ++returned; });
	std::atomic<std::thread::id> completedOn;
	commands->addCompletedHandler([&](auto) { completedOn = std::this_thread::get_id(); });
	commands->commit();
	settle();
	report(returned == 0, "waitUntilCompleted from other threads blocks while the GPU is held");
	gate.release();
	other1.join();
	other2.join();
	report(returned == 2, "waitUntilCompleted from other threads returns when the GPU is done");
	report(eventually([&]() { return completedOn.load() != std::thread::id(); }) && completedOn.load() != std::this_thread::get_id(), "the completed handler runs on another thread");

	// a completed handler that commits another command buffer and waits for it
	std::atomic<bool> chained { false };
	auto outer = makeCommands(queue, buffer);
	outer->addCompletedHandler([&](auto) {
		auto inner = makeCommands(queue, buffer);
		inner->commit();
		inner->waitUntilCompleted();
		chained = true;
	});
	outer->commit();
	report(eventually([&]() { return chained.load(); }), "a completed handler can commit a command buffer and wait for it");

	// a scheduled handler that waits for its own command buffer to finish
	std::atomic<bool> waited { false };
	auto self = makeCommands(queue, buffer);
	self->addScheduledHandler([&](std::shared_ptr<CommandBuffer> commandBuffer) {
		commandBuffer->waitUntilCompleted();
		waited = true;
	});
	self->commit();
	self->waitUntilScheduled();
	report(waited, "a scheduled handler can wait for its command buffer to finish");
}

void retirement() {
	auto queue = device->newCommandQueue();
	std::weak_ptr<Buffer> weakBuffer;
	std::shared_ptr<CommandBuffer> kept;
	{
		auto buffer = makeBuffer();
		weakBuffer = buffer;
		kept = makeCommands(queue, buffer);
		kept->commit();
	}
	kept->waitUntilCompleted();
	report(eventually([&]() { return weakBuffer.expired(); }), "the resources of a finished command buffer are released while the client keeps it");

	// not before: held, the buffer stays
	{
		Gate gate;
		std::weak_ptr<Buffer> held;
		std::shared_ptr<CommandBuffer> commands;
		{
			auto buffer = makeBuffer();
			held = buffer;
			commands = makeCommands(queue, buffer);
			commands->commit();
		}
		settle();
		report(!held.expired(), "the resources of a command buffer in flight are kept");
		gate.release();
		commands->waitUntilCompleted();
		report(eventually([&]() { return held.expired(); }), "the resources are released once it has finished");
	}

	// many in flight
	constexpr int count = 3000;
	std::vector<std::weak_ptr<Buffer>> weak;
	std::vector<std::shared_ptr<CommandBuffer>> commands;
	{
		Gate gate;
		for (int i = 0; i < count; ++i) {
			auto buffer = makeBuffer();
			weak.push_back(buffer);
			commands.push_back(makeCommands(queue, buffer));
			commands.back()->commit();
		}
		settle();
		size_t alive = 0;
		for (const auto& w : weak) {
			alive += !w.expired();
		}
		report(alive == count, "many command buffers in flight keep their resources");
		gate.release();
	}
	commands.back()->waitUntilCompleted();
	report(eventually([&]() {
		for (const auto& w : weak) {
			if (!w.expired()) {
				return false;
			}
		}
		return true;
	}), "many command buffers in flight: every resource is released when they have finished");

	// an uncommitted command buffer that is dropped releases what it holds
	std::weak_ptr<Buffer> unused;
	{
		auto buffer = makeBuffer();
		unused = buffer;
		auto commands = makeCommands(queue, buffer);
	}
	report(unused.expired(), "a command buffer that is never committed releases its resources when it is dropped");
}

} // namespace

int main() {
	Indium::init(nullptr, 0, false);
	int result = 0;
	{
		auto systemDevice = createSystemDefaultDevice();
		if (!systemDevice) {
			std::printf("SKIP no Vulkan device\n");
			Indium::finit();
			return 0;
		}
		device = std::dynamic_pointer_cast<PrivateDevice>(systemDevice);

		std::atomic<bool> keepPolling { true };
		std::thread poller([&]() {
			pollerThread = std::this_thread::get_id();
			while (keepPolling) {
				device->pollEvents(UINT64_MAX);
			}
		});

		LibraryReflection reflection;
		auto& function = reflection.functions["step"];
		function.functionType = FunctionType::Kernel;
		function.bindings.push_back(BindingDescriptor { 0, 0, SIZE_MAX, BindingType::Buffer, TextureAccessType::Sample });
		std::string error;
		auto library = device->newLibrary(compute_spv, compute_spv_len, reflection, &error);
		pipeline = device->newComputePipelineState(library->newFunction("step"));

		commitReturnsWithoutWaiting();
		handlerOrder();
		manyThreads();
		enqueueOrder();
		waitersAndReentrancy();
		retirement();

		keepPolling = false;
		device->wakeupEventLoop();
		poller.join();
		pipeline.reset();
		library.reset();
		systemDevice.reset();
		device.reset();
		result = failures ? 1 : 0;
	}
	Indium::finit();
	return result;
}
