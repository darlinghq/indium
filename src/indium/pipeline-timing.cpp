// Copyright (C) 2026 Lubos Dolezel
#include <indium/pipeline-timing.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <stdexcept>

void Indium::PipelineTimingTable::add(const std::string& name, uint64_t nanoseconds) {
	std::scoped_lock lock(_mutex);
	auto& entry = _timings[name];
	entry.name = name;
	entry.gpuNanoseconds += nanoseconds;
	++entry.spans;
};

std::vector<Indium::PipelineTiming> Indium::PipelineTimingTable::snapshot() const {
	std::scoped_lock lock(_mutex);
	std::vector<PipelineTiming> result;
	result.reserve(_timings.size());
	for (const auto& [name, timing]: _timings) {
		result.push_back(timing);
	}
	return result;
};

Indium::PipelineTimer::PipelineTimer(std::shared_ptr<PrivateDevice> device):
	_device(std::move(device))
{
};

Indium::PipelineTimer::~PipelineTimer() {
	for (auto pool: _pools) {
		DynamicVK::vkDestroyQueryPool(_device->device(), pool, nullptr);
	}
};

uint32_t Indium::PipelineTimer::allocateQuery(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits stage) {
	if (_queryCount % queriesPerPool == 0) {
		VkQueryPoolCreateInfo info {};
		info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = queriesPerPool;
		VkQueryPool pool = VK_NULL_HANDLE;
		if (DynamicVK::vkCreateQueryPool(_device->device(), &info, nullptr, &pool) != VK_SUCCESS) {
			throw std::runtime_error("Cannot create a query pool for pipeline timing");
		}
		DynamicVK::vkResetQueryPool(_device->device(), pool, 0, queriesPerPool);
		_pools.push_back(pool);
	}
	uint32_t query = _queryCount++;
	DynamicVK::vkCmdWriteTimestamp(commandBuffer, stage, _pools[query / queriesPerPool], query % queriesPerPool);
	return query;
};

void Indium::PipelineTimer::begin(VkCommandBuffer commandBuffer, const std::string& name) {
	end(commandBuffer);
	uint32_t first = allocateQuery(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
	_spans.push_back(Span { name, first, 0, false });
};

void Indium::PipelineTimer::end(VkCommandBuffer commandBuffer) {
	if (_spans.empty() || _spans.back().closed) {
		return;
	}
	_spans.back().last = allocateQuery(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	_spans.back().closed = true;
};

void Indium::PipelineTimer::resolve() {
	const double period = _device->timestampPeriod();
	const uint64_t mask = _device->timestampValidBits() >= 64 ? ~0ull : ((1ull << _device->timestampValidBits()) - 1);

	// each result is followed by its availability value
	auto read = [&](uint32_t query, uint64_t& value) {
		uint64_t data[2] = {};
		if (DynamicVK::vkGetQueryPoolResults(_device->device(), _pools[query / queriesPerPool], query % queriesPerPool, 1, sizeof(data), data, sizeof(data), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != VK_SUCCESS || !data[1]) {
			return false;
		}
		value = data[0] & mask;
		return true;
	};

	for (const auto& span: _spans) {
		uint64_t start = 0;
		uint64_t stop = 0;
		if (!span.closed || !read(span.first, start) || !read(span.last, stop) || stop < start) {
			continue;
		}
		_device->pipelineTimingTable().add(span.name, static_cast<uint64_t>(static_cast<double>(stop - start) * period));
	}
};
