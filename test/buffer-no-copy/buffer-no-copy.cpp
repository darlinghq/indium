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
#include <string>
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

// Unaligned pointers and lengths: the pages around the range are imported (or, with `forceFallback`, the import is
// refused and the buffer is a copy), contents() is still the caller's pointer, and what the CPU writes is what the GPU
// reads and the other way round, at the right bytes.
void runUnaligned(uint8_t* memory, ResourceOptions options, const std::shared_ptr<CommandQueue>& queue, bool forceFallback) {
	const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
	device->setBufferNoCopyImportDisabled(forceFallback);
	const std::string mode = forceFallback ? "copying fallback: " : "imported: ";
	auto name = [&](const char* what) { return mode + what; };

	for (size_t misalignment: { size_t(1), size_t(3), size_t(16), size_t(100) }) {
		const size_t length = 3 * page / 2 + 7;
		uint8_t* start = memory + 2 * page + misalignment;
		Calls calls;
		auto buffer = device->newBufferNoCopy(start, length, options, calls.function());
		auto other = device->newBuffer(length, options);
		report(buffer != nullptr && buffer->length() == length && buffer->contents() == start,
			name(("a pointer " + std::to_string(misalignment) + " bytes past a page gives a buffer over the pointer").c_str()).c_str());
		if (!buffer || !other) {
			continue;
		}
		// bytes around the range must stay as they were
		std::memset(memory, 0xee, 8 * page);
		for (size_t i = 0; i < length; ++i) {
			start[i] = static_cast<uint8_t>(i * 7 + misalignment);
		}
		std::memset(other->contents(), 0x22, length);

		auto commands = queue->commandBuffer();
		auto blit = commands->blitCommandEncoder();
		blit->copy(buffer, 0, other, 0, length);
		blit->endEncoding();
		commands->commit();
		commands->waitUntilCompleted();
		bool same = true;
		for (size_t i = 0; i < length; ++i) {
			same = same && static_cast<uint8_t*>(other->contents())[i] == static_cast<uint8_t>(i * 7 + misalignment);
		}
		report(same, name("the GPU reads the bytes the CPU wrote at the unaligned pointer").c_str());

		// the GPU writes: a copy into the buffer at an offset and a fill, odd ones included
		std::memset(other->contents(), 0x44, length);
		const size_t before = misalignment == 1 ? 9 : 4;
		commands = queue->commandBuffer();
		blit = commands->blitCommandEncoder();
		blit->copy(other, 0, buffer, before, 40);
		blit->fillBuffer(buffer, Range<size_t> { 100, 32 }, 0x99);
		blit->fillBuffer(buffer, Range<size_t> { length - length % 4 - 8, 8 }, 0x77);
		blit->endEncoding();
		commands->commit();
		commands->waitUntilCompleted();
		bool copied = true;
		for (size_t i = 0; i < 40; ++i) {
			copied = copied && start[before + i] == 0x44;
		}
		bool filled = true;
		for (size_t i = 100; i < 132; ++i) {
			filled = filled && start[i] == 0x99;
		}
		for (size_t i = length - length % 4 - 8; i < length - length % 4; ++i) {
			filled = filled && start[i] == 0x77;
		}
		report(copied && filled, name("the GPU's copy and fills are seen at the caller's pointer").c_str());
		report(start[before - 1] == static_cast<uint8_t>((before - 1) * 7 + misalignment) && start[before + 40] == static_cast<uint8_t>((before + 40) * 7 + misalignment)
			&& memory[2 * page + misalignment - 1] == 0xee && start[length] == 0xee,
			name("bytes outside the written ranges and outside the buffer are untouched").c_str());

		report(calls.count == 0, name("the deallocator has not run while the buffer is alive").c_str());
		buffer.reset();
		// the command buffers that used the buffer hold it until they retire
		report(eventually([&] { return calls.count == 1; }) && calls.pointer == start && calls.length == length, name("the deallocator runs once with the caller's pointer and length").c_str());
	}

	{
		// a length of a single byte, and a range that ends exactly on a page boundary
		Calls calls;
		auto tiny = device->newBufferNoCopy(memory + page + 5, 1, options, calls.function());
		auto edge = device->newBufferNoCopy(memory + page + 5, page - 5, options, nullptr);
		report(tiny != nullptr && tiny->length() == 1 && edge != nullptr && edge->contents() == memory + page + 5,
			name("a one-byte range and a range ending on a page boundary give buffers").c_str());
	}

	{
		// two buffers over the same pages, one overlapping the other: the GPU writes through one, the CPU sees it in both
		auto first = device->newBufferNoCopy(memory + 3 * page + 8, page, options, nullptr);
		auto second = device->newBufferNoCopy(memory + 3 * page + 8 + 64, page / 2, options, nullptr);
		report(first != nullptr && second != nullptr, name("two buffers over the same pages can both be made").c_str());
		if (first && second) {
			std::memset(memory + 3 * page, 0, 2 * page);
			auto source = device->newBuffer(64, options);
			std::memset(source->contents(), 0x5c, 64);
			auto commands = queue->commandBuffer();
			auto blit = commands->blitCommandEncoder();
			blit->copy(source, 0, second, 0, 64);
			blit->endEncoding();
			commands->commit();
			commands->waitUntilCompleted();
			auto* viaFirst = static_cast<uint8_t*>(first->contents());
			auto* viaSecond = static_cast<uint8_t*>(second->contents());
			report(viaSecond[0] == 0x5c && viaSecond[63] == 0x5c && viaFirst[64] == 0x5c && viaFirst[127] == 0x5c && viaFirst[63] == 0 && viaFirst[128] == 0,
				name("a write through one of two overlapping buffers shows at the other's bytes").c_str());
		}
	}

	if (forceFallback) {
		// the memory handed to a refused import is a copy that the GPU wrote to until the buffer is gone: the final bytes reach the caller
		Calls calls;
		std::atomic<bool> completed { false };
		{
			auto buffer = device->newBufferNoCopy(memory + 6 * page + 3, 64, options, calls.function());
			auto commands = queue->commandBuffer();
			auto blit = commands->blitCommandEncoder();
			blit->fillBuffer(buffer, Range<size_t> { 0, 64 }, 0x31);
			blit->endEncoding();
			commands->addCompletedHandler([&](std::shared_ptr<CommandBuffer>) { completed = true; });
			commands->commit();
			commands->waitUntilCompleted();
		}
		report(eventually([&] { return calls.count == 1; }) && memory[6 * page + 3] == 0x31 && memory[6 * page + 66] == 0x31,
			name("the buffer's last state reaches the caller's memory before the deallocator runs").c_str());
	}

	device->setBufferNoCopyImportDisabled(false);
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
		report(device->newBufferNoCopy(memory, 0, options, calls.function()) == nullptr, "a zero length gives nullptr");
		report(device->newBufferNoCopy(nullptr, page, options, calls.function()) == nullptr, "a null pointer gives nullptr");
		report(device->newBufferNoCopy(memory, page, ResourceOptions::StorageModePrivate, calls.function()) == nullptr, "private storage gives nullptr");
		report(calls.count == 0, "a refused range does not call the deallocator");
	}

	runUnaligned(memory, options, queue, false);
	runUnaligned(memory, options, queue, true);

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
		std::atomic<bool> keepPolling { true };
		std::thread poller([&]() {
			while (keepPolling) {
				device->pollEvents(UINT64_MAX);
			}
		});
		if (device->supportsBufferNoCopy()) {
			run();
		} else {
			// no import: every no-copy buffer is a copy
			std::printf("the device cannot import host memory: only the copying fallback is checked\n");
			const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
			auto* memory = static_cast<uint8_t*>(pages(8));
			runUnaligned(memory, ResourceOptions::StorageModeShared, device->newCommandQueue(), true);
			munmap(memory, 8 * page);
		}
		keepPolling = false;
		device->wakeupEventLoop();
		poller.join();
		device.reset();
		systemDevice.reset();
	}
	Indium::finit();
	std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
