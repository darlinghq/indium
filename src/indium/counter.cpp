// Copyright (C) 2026 Lubos Dolezel
#include <indium/counter.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <indium/blit-command-encoder.hpp>

#include <cstring>

Indium::CounterSampleBuffer::~CounterSampleBuffer() {};

Indium::PrivateCounterSampleBuffer::PrivateCounterSampleBuffer(std::shared_ptr<PrivateDevice> device, VkQueryPool pool, size_t sampleCount):
	_device(std::move(device)),
	_pool(pool),
	_sampleCount(sampleCount)
	{};

Indium::PrivateCounterSampleBuffer::~PrivateCounterSampleBuffer() {
	DynamicVK::vkDestroyQueryPool(_device->device(), _pool, nullptr);
};

std::shared_ptr<Indium::Device> Indium::PrivateCounterSampleBuffer::device() {
	return _device;
};

size_t Indium::PrivateCounterSampleBuffer::sampleCount() {
	return _sampleCount;
};

std::vector<char> Indium::PrivateCounterSampleBuffer::resolveCounterRange(Range<size_t> range) {
	if (range.length == 0 || range.start >= _sampleCount || range.length > _sampleCount - range.start) {
		return {};
	}

	const double period = _device->timestampPeriod();
	const uint32_t validBits = _device->timestampValidBits();
	const uint64_t mask = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1);
	std::vector<char> result(range.length * sizeof(uint64_t));

	for (size_t i = 0; i < range.length; ++i) {
		// each result is followed by its availability value
		uint64_t data[2] = {};
		uint64_t nanoseconds = 0;
		auto status = DynamicVK::vkGetQueryPoolResults(_device->device(), _pool, static_cast<uint32_t>(range.start + i), 1, sizeof(data), data, sizeof(data), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
		if ((status == VK_SUCCESS || status == VK_NOT_READY) && data[1]) {
			nanoseconds = static_cast<uint64_t>(static_cast<double>(data[0] & mask) * period);
		}
		std::memcpy(result.data() + i * sizeof(uint64_t), &nanoseconds, sizeof(nanoseconds));
	}

	return result;
};

void Indium::PrivateCounterSampleBuffer::record(VkCommandBuffer commandBuffer, size_t index, VkPipelineStageFlagBits stage) {
	if (index >= _sampleCount) {
		return;
	}

	// a query has to be reset before it is written again, which Metal does not ask of its callers
	DynamicVK::vkCmdResetQueryPool(commandBuffer, _pool, static_cast<uint32_t>(index), 1);
	DynamicVK::vkCmdWriteTimestamp(commandBuffer, stage, _pool, static_cast<uint32_t>(index));
};

void Indium::PrivateCounterSampleBuffer::record(const std::shared_ptr<CounterSampleBuffer>& buffer, VkCommandBuffer commandBuffer, size_t index, VkPipelineStageFlagBits stage) {
	if (index == counterDontSample) {
		return;
	}
	if (auto privateBuffer = std::dynamic_pointer_cast<PrivateCounterSampleBuffer>(buffer)) {
		privateBuffer->record(commandBuffer, index, stage);
	}
};
