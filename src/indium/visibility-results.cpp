// Copyright (C) 2026 Lubos Dolezel
#include <indium/visibility-results.private.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/command-encoder.private.hpp>
#include <indium/compute-command-encoder.private.hpp>
#include <indium/compute-pipeline.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include "visibility-resolve-spv.hpp"

#include <stdexcept>

namespace {
	constexpr uint32_t modeBoolean = 1;
	constexpr uint32_t modeCounting = 2;
}

bool Indium::VisibilityResults::supported(const std::shared_ptr<PrivateDevice>& device) {
	return device->hostQueryResetEnabled();
};

Indium::VisibilityResults::VisibilityResults(std::shared_ptr<PrivateDevice> device, std::shared_ptr<PrivateBuffer> buffer):
	_device(std::move(device)),
	_buffer(std::move(buffer))
{
};

Indium::VisibilityResults::~VisibilityResults() {
	for (auto pool: _pools) {
		DynamicVK::vkDestroyQueryPool(_device->device(), pool, nullptr);
	}
};

void Indium::VisibilityResults::end(VkCommandBuffer commandBuffer) {
	if (!_active) {
		return;
	}
	_active = false;
	uint32_t query = _spans.back().query;
	DynamicVK::vkCmdEndQuery(commandBuffer, _pools[query / queriesPerPool], query % queriesPerPool);
};

void Indium::VisibilityResults::setMode(VkCommandBuffer commandBuffer, VisibilityResultMode mode, size_t offset) {
	end(commandBuffer);

	if (mode == VisibilityResultMode::Disabled || !_buffer) {
		return;
	}

	// QUIRK: an offset that is not a multiple of 8 is used rounded down to one. The documented contract only
	// allows multiples of 8; macOS 27 (arm64) accepts offset 4 and writes the result at offset 0. Evidence:
	// black-box observation of the public Metal API (tier 2), offsets 4 and 12 into a 64-byte buffer. Test: the
	// check "an offset that is not a multiple of 8 is rounded down" in test/visibility-results (this repository),
	// and test_quirk_unaligned_visibility_offset in test/guest/visibility_results.m of darling-metal.
	offset &= ~(slotSize - 1);
	if (offset > _buffer->length() || _buffer->length() - offset < slotSize) {
		// nothing to write the result to
		return;
	}

	if (_queryCount % queriesPerPool == 0) {
		VkQueryPoolCreateInfo info {};
		info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		info.queryType = VK_QUERY_TYPE_OCCLUSION;
		info.queryCount = queriesPerPool;
		VkQueryPool pool = VK_NULL_HANDLE;
		if (DynamicVK::vkCreateQueryPool(_device->device(), &info, nullptr, &pool) != VK_SUCCESS) {
			throw std::runtime_error("Cannot create a query pool for visibility results");
		}
		// a new pool has undefined contents and cannot be reset inside the render pass
		DynamicVK::vkResetQueryPool(_device->device(), pool, 0, queriesPerPool);
		_pools.push_back(pool);
	}

	uint32_t query = _queryCount++;
	uint32_t slot = offset / slotSize;
	_spans.push_back(Span { slot, mode == VisibilityResultMode::Counting ? modeCounting : modeBoolean, query, _usedSlots.insert(slot).second ? 1u : 0u });

	// a boolean result only needs to know whether anything passed, which every device can tell
	VkQueryControlFlags flags = (mode == VisibilityResultMode::Counting && _device->occlusionQueryPreciseEnabled()) ? VK_QUERY_CONTROL_PRECISE_BIT : 0;
	DynamicVK::vkCmdBeginQuery(commandBuffer, _pools[query / queriesPerPool], query % queriesPerPool, flags);
	_active = true;
};

void Indium::VisibilityResults::resolve(const std::shared_ptr<PrivateCommandBuffer>& commandBuffer) {
	if (_spans.empty()) {
		return;
	}
	auto vkCommandBuffer = commandBuffer->commandBuffer();

	// the 64-bit results of all the queries, in query order
	auto scratch = _device->newBuffer(static_cast<size_t>(_queryCount) * sizeof(uint64_t), ResourceOptions::StorageModePrivate);
	auto privateScratch = std::dynamic_pointer_cast<PrivateBuffer>(scratch);
	for (size_t i = 0; i < _pools.size(); ++i) {
		uint32_t first = static_cast<uint32_t>(i) * queriesPerPool;
		uint32_t count = std::min(queriesPerPool, _queryCount - first);
		DynamicVK::vkCmdCopyQueryPoolResults(vkCommandBuffer, _pools[i], 0, count, privateScratch->buffer(),
			static_cast<VkDeviceSize>(first) * sizeof(uint64_t), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
	}

	VkMemoryBarrier barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	DynamicVK::vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

	// the dispatch is recorded into the segment of the render encoder, which is why it does not begin or end an
	// encoder of its own
	_resolver = std::make_shared<PrivateComputeCommandEncoder>(commandBuffer, ComputePassDescriptor {});
	_resolver->setComputePipelineState(_device->visibilityResolvePipeline());
	uint32_t spanCount = static_cast<uint32_t>(_spans.size());
	_resolver->setBuffer(_buffer, 0, 0);
	_resolver->setBytes(_spans.data(), _spans.size() * sizeof(Span), 1);
	_resolver->setBuffer(scratch, 0, 2);
	_resolver->setBytes(&spanCount, sizeof(spanCount), 3);
	_resolver->dispatchThreadgroups(Size { 1, 1, 1 }, Size { 1, 1, 1 });

	// whatever reads the result buffer later sees it
	finishTrackedEncoder(vkCommandBuffer);
};

std::shared_ptr<Indium::ComputePipelineState> Indium::PrivateDevice::visibilityResolvePipeline() {
	std::scoped_lock lock(_visibilityResolveMutex);
	if (!_visibilityResolvePipeline) {
		FunctionReflection function;
		function.functionType = FunctionType::Kernel;
		for (size_t i = 0; i < 4; ++i) {
			BindingDescriptor binding;
			binding.index = i;
			binding.internalIndex = 0;
			binding.type = BindingType::Buffer;
			function.bindings.push_back(binding);
		}
		LibraryReflection reflection;
		reflection.functions.emplace("visibilityResolve", std::move(function));

		std::string error;
		auto library = newLibrary(visibilityResolveSpirv, sizeof(visibilityResolveSpirv), reflection, &error);
		auto kernel = library ? library->newFunction("visibilityResolve") : nullptr;
		if (!kernel) {
			throw std::runtime_error("Cannot load the visibility result kernel: " + error);
		}
		_visibilityResolvePipeline = newComputePipelineState(kernel);
	}
	return _visibilityResolvePipeline;
};

void Indium::PrivateDevice::releaseCachedObjects() {
	_textureUploader.releaseAll();
	std::scoped_lock lock(_visibilityResolveMutex);
	_visibilityResolvePipeline.reset();
};
