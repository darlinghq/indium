// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in quad-functions.metallib over one threadgroup of 64 threads: the quad-group shuffles of
// 32-bit integers, floats, 8-bit integers and halfs, quad_ballot, also with only some lanes active, and the
// votes of simdgroup and quad-group masks. The host compares every thread's results with what the Metal
// Shading Language specification defines, where a quad-group is four consecutive lanes of a simdgroup.
// Prints SKIP and exits 0 without a Vulkan device.

#include "quad-functions.h"

#include <indium/indium.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	failures += !ok;
}

constexpr size_t threads = 64;

// Runs `kernel` over one threadgroup of `threads` threads and returns the `slots` words each thread wrote.
std::vector<uint32_t> run(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, size_t slots) {
	auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
	if (!pipeline) {
		throw std::runtime_error(std::string(kernel) + " did not load");
	}
	std::vector<uint32_t> poison(threads * slots, 0xA5A5A5A5u);
	auto out = device->newBuffer(poison.data(), poison.size() * sizeof(uint32_t), Indium::ResourceOptions::StorageModeShared);
	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(out, 0, 0);
	encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { threads, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	std::vector<uint32_t> result(poison.size());
	std::memcpy(result.data(), out->contents(), result.size() * sizeof(uint32_t));
	return result;
}

// Runs `kernel`, whose last slot is the simdgroup width, and compares the other slots of every thread t with
// `expect(t, width)`; UNDEFINED marks a slot whose value the specification leaves open.
constexpr uint32_t UNDEFINED = 0xFFFFFFFFu;
void check(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, size_t slots, const std::string& what, const std::function<std::vector<uint32_t>(size_t t, size_t width)>& expect) {
	try {
		auto got = run(device, queue, library, kernel, slots);
		size_t width = got[slots - 1];
		bool ok = width >= 4 && width <= threads && (width & (width - 1)) == 0;
		if (!ok) {
			std::printf("  %s: unusable simdgroup width %zu\n", kernel, width);
		}
		for (size_t t = 0; ok && t < threads; ++t) {
			auto want = expect(t, width);
			want.push_back(width);
			for (size_t k = 0; k < slots; ++k) {
				if (want[k] != UNDEFINED && got[t * slots + k] != want[k]) {
					std::printf("  %s (width %zu): thread %zu slot %zu: got 0x%08x, expected 0x%08x\n", kernel, width, t, k, got[t * slots + k], want[k]);
					ok = false;
				}
			}
		}
		report(ok, std::string(kernel) + ": " + what);
	} catch (const std::exception& e) {
		report(false, std::string(kernel) + ": " + e.what());
	}
}

}

int main() {
	Indium::init(nullptr, 0, false);

	{
		if (Indium::copyAllDevices().empty()) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}

		auto device = Indium::createSystemDefaultDevice();
		std::printf("device: %s\n", device->name().c_str());
		// the bounded wait lets the thread see the flag even if the wakeup comes before it waits
		std::atomic<bool> keepPollingDevice { true };
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(100000000);
			}
		});
		auto queue = device->newCommandQueue();
		auto library = device->newLibrary(quad_functions_metallib, quad_functions_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// the values thread t shuffles: data and filling, as the kernels store them
			auto data = [](size_t t) { return static_cast<uint32_t>(t * 3 + 100); };
			auto filling = [](size_t t) { return static_cast<uint32_t>(t * 3 + 1000); };

			// shuffle from lane 3 - lane and from the next lane (wrapping), shuffle_xor 1 and 2, shuffle_down 1 and 2, shuffle_up
			// 1 and 3, shuffle_and_fill_down with (delta, modulo) (1, 2) and (2, 4), shuffle_and_fill_up with (1, 2) and (3, 4)
			auto shuffles = [&](uint32_t mask) {
				return [&, mask](size_t t, size_t) {
					size_t lane = t % 4, first = t - lane;
					auto down = [&](size_t delta, size_t modulo) {
						size_t position = t % modulo, start = t - position;
						return position + delta < modulo ? data(start + position + delta) : filling(start + position + delta - modulo);
					};
					auto up = [&](size_t delta, size_t modulo) {
						size_t position = t % modulo, start = t - position;
						return position >= delta ? data(start + position - delta) : filling(start + position + modulo - delta);
					};
					std::vector<uint32_t> want {
						data(first + 3 - lane),
						data(first + (lane + 1) % 4),
						data(t ^ 1),
						data(t ^ 2),
						lane + 1 < 4 ? data(t + 1) : data(t),
						lane + 2 < 4 ? data(t + 2) : data(t),
						lane >= 1 ? data(t - 1) : data(t),
						lane >= 3 ? data(t - 3) : data(t),
						down(1, 2),
						down(2, 4),
						up(1, 2),
						up(3, 4),
					};
					for (auto& word: want) {
						word &= mask;
					}
					return want;
				};
			};
			check(device.get(), queue.get(), library.get(), "quad_shuffles", 13, "shuffles of 32-bit integers", shuffles(~0u));
			check(device.get(), queue.get(), library.get(), "quad_shuffles_float", 13, "shuffles of floats", shuffles(~0u));
			check(device.get(), queue.get(), library.get(), "quad_shuffles_char", 13, "shuffles of 8-bit integers", shuffles(0xFF));
			check(device.get(), queue.get(), library.get(), "quad_shuffles_half", 13, "shuffles of halfs", shuffles(~0u));

			// the ballot of the lanes of the simdgroup that are multiples of 3, of all lanes, and of all lanes when only the
			// even ones are active (the odd ones store 77777)
			check(device.get(), queue.get(), library.get(), "quad_ballot", 4, "quad_ballot of some lanes, of all, and with inactive lanes", [](size_t t, size_t width) {
				size_t lane = t % width, first = lane - lane % 4;
				uint32_t multiples = 0;
				for (size_t i = 0; i < 4; ++i) {
					multiples |= (first + i) % 3 == 0 ? 1u << i : 0;
				}
				return std::vector<uint32_t> { multiples, 0xF, lane % 2 == 0 ? 0x5u : 77777u };
			});

			// all() and any() of simdgroup masks: the ballot of all lanes, of all but lane 1, none, all 64 bits, and only the bits
			// past the lanes of the simdgroup; then of quad-group masks: the ballot of all lanes, of all but lane 2, 0xFFF0, 0xF
			// and 0x1
			check(device.get(), queue.get(), library.get(), "votes", 21, "all() and any() of simdgroup and quad-group masks", [](size_t, size_t) {
				return std::vector<uint32_t> { 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 1 };
			});
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
