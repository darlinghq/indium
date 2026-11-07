// Copyright (C) 2026 Lubos Dolezel
//
// Listing devices and reading their properties creates no Vulkan device; the first queue or
// resource does. An event loop polled before that waits without creating the device, can be woken,
// and delivers completions once the device exists.

#include <indium/indium.hpp>
#include <indium/device.private.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unistd.h>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	std::fflush(stdout);
	failures += !ok;
}

// Waits up to five seconds for `flag`.
bool waitFor(const std::atomic<bool>& flag) {
	for (int i = 0; i < 500 && !flag.load(); ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return flag.load();
}

bool created(const std::shared_ptr<Indium::Device>& device) {
	return std::static_pointer_cast<Indium::PrivateDevice>(device)->logicalDeviceCreated();
}

}

int main() {
	// a test that hangs must still end
	std::thread([]() {
		std::this_thread::sleep_for(std::chrono::seconds(60));
		std::printf("FAIL timed out\n");
		std::fflush(stdout);
		_exit(1);
	}).detach();

	Indium::init(nullptr, 0, false);

	{
		auto all = Indium::copyAllDevices();
		if (all.empty()) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}

		bool noneCreated = true;
		for (const auto& device: all) {
			noneCreated = noneCreated && !created(device);
		}
		report(noneCreated, "listing devices creates no Vulkan device");

		auto device = Indium::createSystemDefaultDevice();
		(void)device->name();
		(void)device->vendorID();
		(void)device->deviceType();
		(void)device->memoryHeaps();
		(void)device->deviceUUID();
		(void)device->recommendedMaxWorkingSetSize();
		(void)device->maxBufferLength();
		(void)device->supportsDepth24Stencil8PixelFormat();
		(void)device->supportsDMABufImport();
		(void)device->memoryTypeIndex(~0u, Indium::StorageMode::Shared);
		report(!created(device), "reading device properties creates no Vulkan device");

		auto start = std::chrono::steady_clock::now();
		device->pollEvents(10 * 1000 * 1000);
		auto waited = std::chrono::steady_clock::now() - start;
		report(!created(device) && waited >= std::chrono::milliseconds(5), "polling waits out its timeout without creating the device");

		std::atomic<bool> returned { false };
		std::thread poller([&]() {
			device->pollEvents(UINT64_MAX);
			returned = true;
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		device->wakeupEventLoop();
		report(waitFor(returned) && !created(device), "wakeupEventLoop() ends a poll that waits for the device");
		poller.join();

		std::atomic<bool> keepPolling { true };
		std::thread loop([&]() {
			while (keepPolling) {
				device->pollEvents(UINT64_MAX);
			}
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		auto queue = device->newCommandQueue();
		report(created(device), "creating a command queue creates the device");

		std::atomic<bool> completed { false };
		auto commandBuffer = queue->commandBuffer();
		commandBuffer->addCompletedHandler([&](std::shared_ptr<Indium::CommandBuffer>) {
			completed = true;
		});
		commandBuffer->commit();
		report(waitFor(completed), "an event loop started before the device delivers completions");

		bool othersUnused = true;
		for (const auto& other: all) {
			if (other != device) {
				othersUnused = othersUnused && !created(other);
			}
		}
		report(othersUnused, "the other devices still have no Vulkan device");

		keepPolling = false;
		device->wakeupEventLoop();
		loop.join();
	}

	Indium::finit();

	std::fflush(stdout);
	return failures ? 1 : 0;
}
