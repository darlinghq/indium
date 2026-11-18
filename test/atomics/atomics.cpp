// Copyright (C) 2026 Lubos Dolezel
//
// Kernels from atomics.metallib that use the atomic functions of the Metal Shading Language on device memory
// (buffers of atomic_uint and atomic_int) and threadgroup memory: fetch-and-modify operations, exchange,
// compare-and-exchange, load and store. The results do not depend on the order the threads run in, so each
// is compared with what the specification defines for the whole dispatch.
// Prints SKIP and exits 0 without a Vulkan device.

#include "atomics.h"

#include <indium/indium.hpp>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numeric>
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

using Words = std::vector<uint32_t>;

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
		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});
		auto queue = device->newCommandQueue();
		auto library = device->newLibrary(atomics_metallib, atomics_metallib_len);
		report(library != nullptr, "the library loads");

		// runs `kernel` over groups x size threads with the given buffers (bound from index 0) and returns their contents afterwards
		auto run = [&](const char* kernel, std::vector<Words> buffers, size_t groups, size_t size) {
			auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
			if (!pipeline) {
				throw std::runtime_error(std::string(kernel) + " did not load");
			}
			std::vector<std::shared_ptr<Indium::Buffer>> bound;
			for (auto& words : buffers) {
				bound.push_back(device->newBuffer(words.data(), words.size() * sizeof(uint32_t), Indium::ResourceOptions::StorageModeShared));
			}
			auto commandBuffer = queue->commandBuffer();
			auto encoder = commandBuffer->computeCommandEncoder();
			encoder->setComputePipelineState(pipeline);
			for (size_t i = 0; i < bound.size(); ++i) {
				encoder->setBuffer(bound[i], 0, i);
			}
			encoder->dispatchThreadgroups(Indium::Size { groups, 1, 1 }, Indium::Size { size, 1, 1 });
			encoder->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
			for (size_t i = 0; i < bound.size(); ++i) {
				std::memcpy(buffers[i].data(), bound[i]->contents(), buffers[i].size() * sizeof(uint32_t));
			}
			return buffers;
		};

		auto check = [&](const char* kernel, const std::string& what, const std::function<bool()>& body) {
			try {
				report(body(), std::string(kernel) + ": " + what);
			} catch (const std::exception& e) {
				report(false, std::string(kernel) + ": " + e.what());
			}
		};

		check("add_counters", "add and subtract from every thread of 4 groups", [&] {
			auto result = run("add_counters", { { 0, 0, 1000 } }, 4, 64)[0];
			return result == Words { 256, 32640, 744 };
		});

		check("bit_counters", "min, max, or, and, xor on unsigned values", [&] {
			auto result = run("bit_counters", { { 0xFFFFFFFFu, 0, 0, 0xFFFFFFFFu, 0 } }, 1, 64)[0];
			return result == Words { 5, 68, 0xFFFFFFFFu, 0, 0 };
		});

		check("signed_counters", "min and max on signed values", [&] {
			auto result = run("signed_counters", { { static_cast<uint32_t>(INT_MAX), static_cast<uint32_t>(INT_MIN) } }, 1, 64)[0];
			return static_cast<int32_t>(result[0]) == -20 && static_cast<int32_t>(result[1]) == 43;
		});

		check("tickets", "every thread's fetch-and-add returns a different value", [&] {
			auto result = run("tickets", { { 0 }, Words(128, 0xA5A5A5A5u) }, 2, 64);
			std::sort(result[1].begin(), result[1].end());
			Words want(128);
			std::iota(want.begin(), want.end(), 0u);
			return result[0][0] == 128 && result[1] == want;
		});

		check("exchange", "each exchange returns the value the previous one stored", [&] {
			auto result = run("exchange", { { 0 }, Words(64, 0xA5A5A5A5u) }, 1, 64);
			// the values that went through the slot: the initial 0 and every thread's id + 1
			Words seen = result[1];
			seen.push_back(result[0][0]);
			std::sort(seen.begin(), seen.end());
			Words want(65);
			std::iota(want.begin(), want.end(), 0u);
			return seen == want;
		});

		check("claim", "compare-and-exchange succeeds for exactly one thread", [&] {
			auto result = run("claim", { { 0 }, Words(64, 0xA5A5A5A5u) }, 1, 64);
			size_t winners = std::count(result[1].begin(), result[1].end(), 1u);
			size_t winner = std::find(result[1].begin(), result[1].end(), 1u) - result[1].begin();
			if (!(winners == 1 && result[0][0] == winner + 1)) {
				std::printf("winners %zu, flag %u, first winner %zu\n", winners, result[0][0], winner);
			}
			return winners == 1 && result[0][0] == winner + 1;
		});

		check("claim_from_seven", "compare-and-exchange from a non-zero value; a failure reports the value found", [&] {
			auto result = run("claim_from_seven", { { 7 }, Words(64, 0xA5A5A5A5u) }, 1, 64);
			size_t winners = std::count(result[1].begin(), result[1].end(), 1u);
			size_t winner = std::find(result[1].begin(), result[1].end(), 1u) - result[1].begin();
			// the others see what the winner stored: its id + 100
			bool others = true;
			for (size_t i = 0; i < 64; ++i) {
				others = others && (i == winner || result[1][i] == 1000 + 100 + winner);
			}
			if (!(winners == 1 && result[0][0] == winner + 100 && others)) {
				std::printf("winners %zu, flag %u, winner %zu, out[0] %u out[1] %u\n", winners, result[0][0], winner, result[1][0], result[1][1]);
			}
			return winners == 1 && result[0][0] == winner + 100 && others;
		});

		check("load_store", "a stored value is loaded back", [&] {
			auto result = run("load_store", { Words(64, 0xA5A5A5A5u), Words(64, 0xA5A5A5A5u) }, 1, 64);
			Words want(64);
			for (size_t i = 0; i < want.size(); ++i) {
				want[i] = i * 3 + 1;
			}
			return result[0] == want && result[1] == want;
		});

		check("group_counters", "threadgroup atomics count each group's own threads", [&] {
			auto result = run("group_counters", { Words(6, 0xA5A5A5A5u) }, 3, 32)[0];
			return result == Words { 32, 496, 32, 496, 32, 496 };
		});

		check("min_max_64", "64-bit minimum and maximum of values above 32 bits", [&] {
			// two 64-bit counters as pairs of words, low word first: a maximum from 0 and a minimum from the largest value
			auto result = run("min_max_64", { { 0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu } }, 1, 64)[0];
			uint64_t maximum = static_cast<uint64_t>(result[1]) << 32 | result[0];
			uint64_t minimum = static_cast<uint64_t>(result[3]) << 32 | result[2];
			return maximum == ((63ull << 32) | 5) && minimum == 0x100000000ull + 100 - 63;
		});

		check("fences", "stores around device and threadgroup fences all happen", [&] {
			auto result = run("fences", { Words(64 * 3, 0xA5A5A5A5u) }, 1, 64)[0];
			bool ok = true;
			for (size_t i = 0; i < 64; ++i) {
				ok = ok && result[i * 3] == 1 && result[i * 3 + 1] == 2 && result[i * 3 + 2] == 3;
			}
			return ok;
		});

		auto asFloat = [](uint32_t word) {
			float value;
			std::memcpy(&value, &word, sizeof(value));
			return value;
		};
		auto floatBits = [](float value) {
			uint32_t word;
			std::memcpy(&word, &value, sizeof(word));
			return word;
		};

		check("float_add", "float add and subtract from every thread", [&] {
			// 64 threads add 1 to the first and subtract 0.5 from the second (all sums are exact in a float)
			auto result = run("float_add", { { floatBits(0), floatBits(100) } }, 1, 64)[0];
			return asFloat(result[0]) == 64.0f && asFloat(result[1]) == 68.0f;
		});

		check("float_exchange", "each float exchange returns the value the previous one stored", [&] {
			auto result = run("float_exchange", { { floatBits(0) }, Words(64, 0xA5A5A5A5u) }, 1, 64);
			// the values that went through the slot: the initial 0, then every thread's id + 1; one of them is left in the slot
			std::vector<float> seen;
			for (auto word: result[1]) {
				seen.push_back(asFloat(word));
			}
			seen.push_back(asFloat(result[0][0]));
			std::sort(seen.begin(), seen.end());
			bool ok = true;
			for (size_t i = 0; i < seen.size(); ++i) {
				ok = ok && seen[i] == static_cast<float>(i);
			}
			return ok;
		});

		check("float_load_store", "a stored float is loaded back", [&] {
			auto result = run("float_load_store", { Words(64, 0xA5A5A5A5u), Words(64, 0xA5A5A5A5u) }, 1, 64);
			bool ok = true;
			for (size_t i = 0; i < 64; ++i) {
				ok = ok && asFloat(result[0][i]) == i * 2.5f + 1.0f && asFloat(result[1][i]) == i * 2.5f + 1.0f;
			}
			return ok;
		});

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
