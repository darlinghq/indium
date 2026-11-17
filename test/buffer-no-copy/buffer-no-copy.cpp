// Copyright (C) 2026 Lubos Dolezel
//
// Device::newBufferNoCopy: a buffer over memory the caller owns (VK_EXT_external_memory_host). The
// buffer's contents() is the caller's pointer, CPU writes are what the GPU reads and GPU writes are what
// the CPU reads, the deallocator runs once and only after the GPU is done with the buffer, and memory
// the device cannot import gives nullptr and no deallocator call. On a device without the extension the
// checks are skipped.

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>

#include <sys/mman.h>
#include <unistd.h>

using namespace Indium;
namespace DynamicVK = Indium::DynamicVK;

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	std::fflush(stdout);
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

std::shared_ptr<PrivateDevice> device;

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

struct Calls {
	std::atomic<int> count { 0 };
	std::atomic<void*> pointer { nullptr };
	std::atomic<size_t> length { 0 };
	std::function<void(void*, size_t)> function() {
		return [this](void* p, size_t l) {
			pointer = p;
			length = l;
			++count;
		};
	}
};

void* pages(size_t count) {
	void* memory = mmap(nullptr, count * static_cast<size_t>(sysconf(_SC_PAGESIZE)), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	return memory == MAP_FAILED ? nullptr : memory;
}

void run() {
	const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
	auto* memory = static_cast<uint8_t*>(pages(8));
	auto queue = device->newCommandQueue();
	auto options = ResourceOptions::StorageModeShared;

	{
		Calls calls;
		auto buffer = device->newBufferNoCopy(memory, 2 * page, options, calls.function());
		report(buffer != nullptr, "an aligned shared range gives a buffer");
		if (buffer) {
			report(buffer->contents() == memory && buffer->length() == 2 * page, "contents() is the caller's pointer and length() the length");
			memory[5] = 0x5a;
			report(static_cast<uint8_t*>(buffer->contents())[5] == 0x5a, "a CPU write through the pointer is visible through contents()");
			static_cast<uint8_t*>(buffer->contents())[9] = 0xa5;
			report(memory[9] == 0xa5, "a write through contents() is visible through the pointer");
			report(buffer->gpuAddress() != 0, "the buffer has a device address");
			buffer->didModifyRange(Range<size_t> { 0, 16 });
			report(calls.count == 0, "the deallocator has not run while the buffer is alive");
		}
		buffer.reset();
		report(calls.count == 1 && calls.pointer == memory && calls.length == 2 * page, "releasing the buffer calls the deallocator once with the pointer and length");
	}

	{
		// a length that is not a whole number of pages is allowed; the pointer and length passed back are the caller's
		Calls calls;
		auto buffer = device->newBufferNoCopy(memory, page + 1, options, calls.function());
		report(buffer != nullptr && buffer->length() == page + 1, "a length that is not a multiple of the page size gives a buffer of that length");
		buffer.reset();
		report(calls.count == 1 && calls.length == page + 1, "its deallocator gets the length that was given");
	}

	{
		// the managed storage mode shares the same coherent memory
		Calls calls;
		auto buffer = device->newBufferNoCopy(memory, page, ResourceOptions::StorageModeManaged, calls.function());
		report(buffer != nullptr && buffer->contents() == memory, "a managed range gives a buffer over the pointer");
	}

	{
		// refusals give nullptr and never call the deallocator
		Calls calls;
		report(device->newBufferNoCopy(memory + 16, page, options, calls.function()) == nullptr, "an unaligned pointer gives nullptr");
		report(device->newBufferNoCopy(memory, 0, options, calls.function()) == nullptr, "a zero length gives nullptr");
		report(device->newBufferNoCopy(nullptr, page, options, calls.function()) == nullptr, "a null pointer gives nullptr");
		report(device->newBufferNoCopy(memory, page, ResourceOptions::StorageModePrivate, calls.function()) == nullptr, "private storage gives nullptr");
		report(calls.count == 0, "a refused range does not call the deallocator");
	}

	{
		// no deallocator is fine
		auto buffer = device->newBufferNoCopy(memory, page, options, nullptr);
		report(buffer != nullptr, "an empty deallocator is accepted");
	}

	{
		// the GPU writes the CPU's bytes into another buffer, and the CPU sees what the GPU wrote into the no-copy buffer
		auto buffer = device->newBufferNoCopy(memory + 2 * page, page, options, nullptr);
		auto other = device->newBuffer(page, options);
		if (buffer && other) {
			std::memset(memory + 2 * page, 0x11, page);
			std::memset(other->contents(), 0x22, page);
			auto commands = queue->commandBuffer();
			auto blit = commands->blitCommandEncoder();
			blit->copy(buffer, 0, other, 0, page / 2);
			blit->fillBuffer(buffer, Range<size_t> { page / 2, page / 2 }, 0x77);
			blit->endEncoding();
			commands->commit();
			commands->waitUntilCompleted();
			auto* mine = memory + 2 * page;
			auto* theirs = static_cast<uint8_t*>(other->contents());
			report(theirs[0] == 0x11 && theirs[page / 2 - 1] == 0x11 && theirs[page / 2] == 0x22, "the GPU reads the bytes the CPU wrote to the caller's memory");
			report(mine[0] == 0x11 && mine[page / 2 - 1] == 0x11 && mine[page / 2] == 0x77 && mine[page - 1] == 0x77, "the GPU's writes show in the caller's memory, the rest is untouched");
		} else {
			report(false, "buffers for the GPU round trip");
		}
	}

	{
		// the deallocator waits for the GPU: the buffer is released while a command buffer that uses it is held back
		Calls calls;
		std::atomic<bool> completed { false };
		{
			Gate gate;
			auto buffer = device->newBufferNoCopy(memory + 4 * page, page, options, calls.function());
			auto commands = queue->commandBuffer();
			auto blit = commands->blitCommandEncoder();
			blit->fillBuffer(buffer, Range<size_t> { 0, page }, 0x33);
			blit->endEncoding();
			blit.reset();
			commands->addCompletedHandler([&](std::shared_ptr<CommandBuffer>) { completed = true; });
			commands->commit();
			buffer.reset();
			commands.reset();
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			report(calls.count == 0 && !completed, "the deallocator does not run while the GPU has not finished with the buffer");
			gate.release();
			report(eventually([&] { return calls.count == 1; }), "the deallocator runs once the GPU has finished");
			report(completed || eventually([&] { return completed.load(); }), "the command buffer completed");
		}
		report(calls.count == 1 && memory[4 * page] == 0x33 && memory[5 * page - 1] == 0x33, "the GPU's fill reached the memory before the deallocator ran");
	}

	munmap(memory, 8 * page);
}

} // namespace

int main() {
	Indium::init(nullptr, 0, false);
	{
		auto systemDevice = createSystemDefaultDevice();
		if (!systemDevice) {
			std::printf("SKIP no Vulkan device\n");
			Indium::finit();
			return 0;
		}
		device = std::dynamic_pointer_cast<PrivateDevice>(systemDevice);
		if (!device->supportsBufferNoCopy()) {
			void* memory = pages(1);
			report(device->newBufferNoCopy(memory, static_cast<size_t>(sysconf(_SC_PAGESIZE)), ResourceOptions::StorageModeShared, nullptr) == nullptr,
				"without VK_EXT_external_memory_host no buffer is made");
			std::printf("SKIP the device cannot import host memory\n");
		} else {
			std::atomic<bool> keepPolling { true };
			std::thread poller([&]() {
				while (keepPolling) {
					device->pollEvents(UINT64_MAX);
				}
			});
			run();
			keepPolling = false;
			device->wakeupEventLoop();
			poller.join();
		}
		device.reset();
		systemDevice.reset();
	}
	Indium::finit();
	std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
