// Copyright (C) 2026 Lubos Dolezel
//
// Counter sampling with the timestamp counter set:
//  - the device offers one counter set, "timestamp", holding the counter "GPUTimestamp", and samples at stage,
//    dispatch and blit boundaries;
//  - a sample buffer holds sampleCount 64-bit nanosecond timestamps; samples never written read as zero;
//  - a blit pass descriptor's start and end samples, a render pass descriptor's four stage samples and
//    sampleCounters() on a blit encoder store timestamps, which increase in the order they were taken;
//  - a descriptor without the timestamp set, or with a sample count outside 1...4096, is refused as Invalid;
//  - resolving an empty or out-of-bounds range gives nothing;
//  - sampleTimestamps() gives two clocks that do not go backwards.
// Prints SKIP and exits 0 without a Vulkan device or without timestamp support.

#include <indium/indium.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

std::vector<uint64_t> resolve(const std::shared_ptr<Indium::CounterSampleBuffer>& buffer, size_t start, size_t length) {
	auto bytes = buffer->resolveCounterRange({ start, length });
	std::vector<uint64_t> values(bytes.size() / sizeof(uint64_t));
	std::memcpy(values.data(), bytes.data(), values.size() * sizeof(uint64_t));
	return values;
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
		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		auto sets = device->counterSets();
		if (sets.empty()) {
			std::printf("SKIP no timestamp support\n");
		} else {
			report(sets.size() == 1 && sets[0].name == "timestamp" && sets[0].counters.size() == 1 && sets[0].counters[0].name == "GPUTimestamp", "the timestamp set holds GPUTimestamp");
			report(device->supportsCounterSampling(Indium::CounterSamplingPoint::AtStageBoundary), "stage boundary sampling is supported");
			report(device->supportsCounterSampling(Indium::CounterSamplingPoint::AtDispatchBoundary), "dispatch boundary sampling is supported");
			report(device->supportsCounterSampling(Indium::CounterSamplingPoint::AtBlitBoundary), "blit boundary sampling is supported");
			report(!device->supportsCounterSampling(Indium::CounterSamplingPoint::AtDrawBoundary), "draw boundary sampling is not supported");

			Indium::CounterSampleBufferDescriptor descriptor;
			descriptor.counterSetName = Indium::counterSetTimestamp;
			descriptor.sampleCount = 6;
			descriptor.label = "samples";
			auto samples = device->newCounterSampleBuffer(descriptor);
			report(samples && samples->sampleCount() == 6, "a sample buffer holds the requested number of samples");
			report(samples && samples->device() == device, "the sample buffer belongs to its device");
			auto fresh = resolve(samples, 0, 6);
			report(fresh.size() == 6 && std::all_of(fresh.begin(), fresh.end(), [](uint64_t v) { return v == 0; }), "unwritten samples read as zero");
			report(resolve(samples, 0, 0).empty() && resolve(samples, 5, 2).empty() && resolve(samples, 6, 1).empty(), "an empty or out-of-bounds range resolves to nothing");

			Indium::CounterSampleBufferFailure failure;
			Indium::CounterSampleBufferDescriptor bad = descriptor;
			bad.sampleCount = 0;
			report(!device->newCounterSampleBuffer(bad, &failure) && failure.error == Indium::CounterSampleBufferError::Invalid, "zero samples is Invalid");
			bad.sampleCount = 4097;
			failure = {};
			report(!device->newCounterSampleBuffer(bad, &failure) && failure.error == Indium::CounterSampleBufferError::Invalid, "more than 4096 samples is Invalid");
			bad.sampleCount = 4096;
			report(device->newCounterSampleBuffer(bad) != nullptr, "4096 samples is allowed");
			bad.counterSetName = "";
			failure = {};
			report(!device->newCounterSampleBuffer(bad, &failure) && failure.error == Indium::CounterSampleBufferError::Invalid && !failure.message.empty(), "no counter set is Invalid with a message");
			bad.counterSetName = "statistic";
			report(!device->newCounterSampleBuffer(bad), "a counter set the device does not have is refused");

			uint64_t cpu1 = 0, gpu1 = 0, cpu2 = 0, gpu2 = 0;
			device->sampleTimestamps(&cpu1, &gpu1);
			device->sampleTimestamps(&cpu2, &gpu2);
			report(cpu1 != 0 && gpu1 != 0 && cpu2 >= cpu1 && gpu2 >= gpu1, "sampleTimestamps gives clocks that do not go backwards");

			auto queue = device->newCommandQueue();
			auto data = device->newBuffer(1 << 20, Indium::ResourceOptions::StorageModeShared);

			// blit pass: start at 0, work, a sample from the encoder at 1, end at 2, then a second pass at 3 and 4
			{
				auto commands = queue->commandBuffer();
				Indium::BlitPassDescriptor pass;
				pass.sampleBufferAttachments[0] = { samples, 0, 2 };
				auto blit = commands->blitCommandEncoder(pass);
				blit->fillBuffer(data, { 0, 1 << 20 }, 7);
				blit->sampleCounters(samples, 1, true);
				blit->endEncoding();
				Indium::BlitPassDescriptor second;
				second.sampleBufferAttachments[0] = { samples, 3, 4 };
				auto blit2 = commands->blitCommandEncoder(second);
				blit2->fillBuffer(data, { 0, 1 << 20 }, 9);
				blit2->endEncoding();
				commands->commit();
				commands->waitUntilCompleted();

				auto values = resolve(samples, 0, 5);
				report(values.size() == 5 && values[0] != 0, "a blit pass stores its start sample");
				bool increasing = values.size() == 5;
				for (size_t i = 1; increasing && i < values.size(); ++i) {
					increasing = values[i] >= values[i - 1];
				}
				report(increasing, "the blit samples increase in the order they were taken");
				report(resolve(samples, 5, 1)[0] == 0, "a sample no pass named stays zero");
			}

			// a pass with the default (do not sample) indices samples nothing
			{
				auto commands = queue->commandBuffer();
				Indium::BlitPassDescriptor pass;
				pass.sampleBufferAttachments[0].sampleBuffer = samples;
				auto blit = commands->blitCommandEncoder(pass);
				blit->fillBuffer(data, { 0, 16 }, 1);
				blit->endEncoding();
				commands->commit();
				commands->waitUntilCompleted();
				report(true, "a pass whose sample indices are counterDontSample completes");
			}

			// render pass: the vertex and fragment starts at 0 and 1 and ends at 2 and 3
			{
				auto textureDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 16, 16, false);
				textureDescriptor.usage = Indium::TextureUsage::RenderTarget;
				auto color = device->newTexture(textureDescriptor);
				auto fresh2 = device->newCounterSampleBuffer(descriptor);
				auto commands = queue->commandBuffer();
				Indium::RenderPassDescriptor pass {};
				pass.colorAttachments.emplace_back();
				pass.colorAttachments[0].texture = color;
				pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
				pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
				pass.colorAttachments[0].clearColor = Indium::ClearColor(0, 0, 0, 0);
				pass.sampleBufferAttachments[0].sampleBuffer = fresh2;
				pass.sampleBufferAttachments[0].startOfVertexSampleIndex = 0;
				pass.sampleBufferAttachments[0].startOfFragmentSampleIndex = 1;
				pass.sampleBufferAttachments[0].endOfVertexSampleIndex = 2;
				pass.sampleBufferAttachments[0].endOfFragmentSampleIndex = 3;
				auto render = commands->renderCommandEncoder(pass);
				render->endEncoding();
				commands->commit();
				commands->waitUntilCompleted();

				auto values = resolve(fresh2, 0, 4);
				report(values.size() == 4 && values[0] != 0 && values[1] != 0 && values[2] >= values[0] && values[3] >= values[1], "a render pass stores its stage samples in order");
			}
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
