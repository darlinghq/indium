// Copyright (C) 2026 Darling Developers
//
// BlitCommandEncoder::fillBuffer on byte ranges whose start or length is not a multiple of 4, ranges within a single
// word, long ranges with ragged ends, and empty ranges. Every byte in the range gets the value and the bytes
// around it are untouched, in a buffer of its own, in a no-copy buffer whose memory does not start on a word, and in
// a 3-byte buffer. A copy recorded after a fill reads the filled bytes.

#include <indium/indium.hpp>
#include <indium/buffer.private.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <sys/mman.h>
#include <unistd.h>

using namespace Indium;

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	std::fflush(stdout);
	failures += !ok;
}

constexpr uint8_t background = 0xee;
constexpr uint8_t value = 0x5a;

struct Case {
	size_t start;
	size_t length;
};

const Case cases[] = {
	{ 8, 16 },      // aligned
	{ 1, 8 },       // unaligned start
	{ 8, 7 },       // unaligned length
	{ 3, 10 },      // both
	{ 5, 1 },       // within one word
	{ 6, 2 },
	{ 9, 3 },
	{ 4, 3 },       // from the start of a word
	{ 5, 3 },       // to the end of a word
	{ 3, 2 },       // across a word boundary, no whole word
	{ 3, 65531 },   // long, both ends ragged
	{ 7, 0 },       // empty
};

bool fillsExactly(const std::shared_ptr<CommandQueue>& queue, const std::shared_ptr<Buffer>& buffer, Case c) {
	auto* bytes = static_cast<uint8_t*>(buffer->contents());
	std::memset(bytes, background, buffer->length());
	auto commands = queue->commandBuffer();
	auto blit = commands->blitCommandEncoder();
	blit->fillBuffer(buffer, Range<size_t> { c.start, c.length }, value);
	blit->endEncoding();
	commands->commit();
	commands->waitUntilCompleted();

	bool ok = true;
	for (size_t i = 0; i < buffer->length(); ++i) {
		ok = ok && bytes[i] == (i >= c.start && i < c.start + c.length ? value : background);
	}
	return ok;
}

void run(const std::shared_ptr<CommandQueue>& queue, const std::shared_ptr<Buffer>& buffer, const std::string& kind) {
	for (const auto& c: cases) {
		report(fillsExactly(queue, buffer, c), kind + ": a fill of " + std::to_string(c.length) + " bytes at " + std::to_string(c.start) + " writes exactly those bytes");
	}
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

		std::atomic<bool> keepPollingDevice { true };
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		auto queue = device->newCommandQueue();
		const size_t length = 65536 + 16;
		run(queue, device->newBuffer(length, ResourceOptions::StorageModeShared), "buffer");

		{
			// one byte into a page, so that an imported buffer starts in the middle of a word of its VkBuffer
			const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
			const size_t mapped = (length + 1 + page - 1) / page * page;
			void* memory = mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			report(memory != MAP_FAILED, "memory for a no-copy buffer");
			if (memory != MAP_FAILED) {
				// the command buffers that used it hold the buffer until they retire: unmap only once it is gone
				auto noCopy = device->newBufferNoCopy(static_cast<uint8_t*>(memory) + 1, length, ResourceOptions::StorageModeShared,
					[memory, mapped](void*, size_t) { munmap(memory, mapped); });
				report(noCopy != nullptr, "a no-copy buffer one byte into a page");
				if (noCopy) {
					const auto vkOffset = std::static_pointer_cast<PrivateBuffer>(noCopy)->bufferOffset();
					std::printf("the no-copy buffer starts at byte %llu of its VkBuffer\n", static_cast<unsigned long long>(vkOffset));
					run(queue, noCopy, "no-copy buffer");
				} else {
					munmap(memory, mapped);
				}
			}
		}

		// a whole buffer shorter than a word
		auto tiny = device->newBuffer(3, ResourceOptions::StorageModeShared);
		report(fillsExactly(queue, tiny, Case { 0, 3 }) && fillsExactly(queue, tiny, Case { 1, 1 }), "fills of a 3-byte buffer write exactly their bytes");

		{
			// a copy after a fill in the same encoder sees the filled bytes, the ragged ends included
			auto source = device->newBuffer(64, ResourceOptions::StorageModeShared);
			auto destination = device->newBuffer(64, ResourceOptions::StorageModeShared);
			std::memset(source->contents(), background, 64);
			std::memset(destination->contents(), 0, 64);
			auto commands = queue->commandBuffer();
			auto blit = commands->blitCommandEncoder();
			blit->fillBuffer(source, Range<size_t> { 1, 13 }, value);
			blit->copy(source, 0, destination, 0, 64);
			blit->endEncoding();
			commands->commit();
			commands->waitUntilCompleted();
			const auto* copied = static_cast<const uint8_t*>(destination->contents());
			bool ok = true;
			for (size_t i = 0; i < 64; ++i) {
				ok = ok && copied[i] == (i >= 1 && i < 14 ? value : background);
			}
			report(ok, "a copy after a fill in the same encoder reads the filled bytes");
		}

		queue.reset();
		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
	return failures ? 1 : 0;
}
