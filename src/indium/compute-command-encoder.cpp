#include <indium/compute-command-encoder.private.hpp>
#include <indium/device.private.hpp>
#include <indium/counter.private.hpp>
#include <indium/event.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <indium/pipeline-timing.private.hpp>

Indium::ComputeCommandEncoder::~ComputeCommandEncoder() {};

Indium::PrivateComputeCommandEncoder::PrivateComputeCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const ComputePassDescriptor& descriptor):
	_privateCommandBuffer(commandBuffer),
	_privateDevice(std::dynamic_pointer_cast<PrivateDevice>(commandBuffer->device())),
	_descriptor(descriptor),
	_descriptorPools(_privateDevice->device())
{
	for (const auto& [index, attachment]: _descriptor.sampleBufferAttachments) {
		PrivateCounterSampleBuffer::record(attachment.sampleBuffer, commandBuffer->commandBuffer(), attachment.startOfEncoderSampleIndex, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
	}
	_tracked = commandBuffer->hazardsTracked();
	if (_tracked && _descriptor.dispatchType == DispatchType::Concurrent) {
		// the dispatches may run together: only what came before the encoder is waited for
		commandBuffer->hazards().beginUnorderedGroup();
	}
};

Indium::PrivateComputeCommandEncoder::~PrivateComputeCommandEncoder() {
	for (auto& pipeline: _savedPipelines) {
		DynamicVK::vkDestroyPipeline(_privateDevice->device(), pipeline, nullptr);
	}
};

void Indium::PrivateComputeCommandEncoder::endEncoding() {
	if (auto commandBuffer = _privateCommandBuffer.lock()) {
		for (const auto& [index, attachment]: _descriptor.sampleBufferAttachments) {
			PrivateCounterSampleBuffer::record(attachment.sampleBuffer, commandBuffer->commandBuffer(), attachment.endOfEncoderSampleIndex, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		}
		if (_tracked) {
			// nothing is recorded: the next command that uses what this encoder wrote waits for it
			commandBuffer->hazards().endUnorderedGroup();
		} else {
			commandBuffer->endUntrackedEncoder();
		}
		commandBuffer->endEncoderSegment(_functionResources.usedTextures, {});
	}
};

void Indium::PrivateComputeCommandEncoder::updateFence(const std::shared_ptr<Fence>& fence) {
	// see checkFence(): nothing to record
	checkFence(fence, _privateDevice);
};

void Indium::PrivateComputeCommandEncoder::waitForFence(const std::shared_ptr<Fence>& fence) {
	checkFence(fence, _privateDevice);
	if (auto commandBuffer = _privateCommandBuffer.lock()) {
		// everything the fence's encoders did, tracked resources or not, is ordered before what follows
		commandBuffer->hazards().fullBarrier(commandBuffer->commandBuffer());
		if (_tracked && _descriptor.dispatchType == DispatchType::Concurrent) {
			commandBuffer->hazards().beginUnorderedGroup();
		}
	}
};

void Indium::PrivateComputeCommandEncoder::beginTimedDispatch(VkCommandBuffer commandBuffer) {
	if (!_privateDevice->pipelineTimingEnabled()) {
		return;
	}
	if (!_timer) {
		_timer = std::make_shared<PipelineTimer>(_privateDevice);
		_privateCommandBuffer.lock()->addPipelineTimer(_timer);
	}
	_timer->begin(commandBuffer, _pso->timingName());
};

void Indium::PrivateComputeCommandEncoder::endTimedDispatch(VkCommandBuffer commandBuffer) {
	if (_timer) {
		_timer->end(commandBuffer);
	}
};

void Indium::PrivateComputeCommandEncoder::setComputePipelineState(std::shared_ptr<ComputePipelineState> state) {
	_pso = std::dynamic_pointer_cast<PrivateComputePipelineState>(state);
};

void Indium::PrivateComputeCommandEncoder::setBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) {
	_functionResources.setBuffer(buffer, offset, index);
};

void Indium::PrivateComputeCommandEncoder::setBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setBuffer(buffers[i], offsets[i], range.start + i);
	}
};

void Indium::PrivateComputeCommandEncoder::setBufferOffset(size_t offset, size_t index) {
	_functionResources.setBufferOffset(offset, index);
};

void Indium::PrivateComputeCommandEncoder::setBytes(const void* bytes, size_t length, size_t index) {
	_functionResources.setBytes(_privateDevice, _scratch, _keepAliveBuffers, bytes, length, index);
};

void Indium::PrivateComputeCommandEncoder::setSamplerState(std::shared_ptr<SamplerState> state, size_t index) {
	_functionResources.setSamplerState(state, std::nullopt, index);
};

void Indium::PrivateComputeCommandEncoder::setSamplerState(std::shared_ptr<SamplerState> state, float lodMinClamp, float lodMaxClamp, size_t index) {
	_functionResources.setSamplerState(state, std::make_pair(lodMinClamp, lodMaxClamp), index);
};

void Indium::PrivateComputeCommandEncoder::setSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setSamplerState(states[i], range.start + i);
	}
};

void Indium::PrivateComputeCommandEncoder::setSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setSamplerState(states[i], lodMinClamps[i], lodMaxClamps[i], range.start + i);
	}
};

void Indium::PrivateComputeCommandEncoder::setTexture(std::shared_ptr<Texture> texture, size_t index) {
	_functionResources.setTexture(texture, index);
};

void Indium::PrivateComputeCommandEncoder::setTextures(const std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setTexture(textures[i], range.start + i);
	}
};

void Indium::PrivateComputeCommandEncoder::setThreadgroupMemoryLength(size_t length, size_t index) {
	// The length sizes the kernel's threadgroup memory argument at `index`, in the pipelines of the dispatches
	// that follow (see PrivateComputePipelineState::createPipeline()).
	_threadgroupMemoryLengths[index] = length;
};

void Indium::PrivateComputeCommandEncoder::dispatchThreadgroups(Size threadgroupsPerGrid, Size threadsPerThreadgroup) {
	dispatchThreadgroups(threadgroupsPerGrid, threadsPerThreadgroup, std::nullopt);
};

void Indium::PrivateComputeCommandEncoder::dispatchThreadgroups(Size threadgroupsPerGrid, Size threadsPerThreadgroup, const std::optional<Size>& gridSize) {
	auto buf = _privateCommandBuffer.lock();

	auto pipeline = _pso->createPipeline(threadsPerThreadgroup, _threadgroupMemoryLengths, gridSize, _stageInRegion);
	_savedPipelines.push_back(pipeline);

	DynamicVK::vkCmdBindPipeline(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	// TODO: avoid re-binding descriptors on every dispatch
	updateBindings();

	orderDispatch(nullptr, 0);

	beginTimedDispatch(buf->commandBuffer());
	DynamicVK::vkCmdDispatch(buf->commandBuffer(), threadgroupsPerGrid.width, threadgroupsPerGrid.height, threadgroupsPerGrid.depth);
	endTimedDispatch(buf->commandBuffer());

	// see PrivateRenderCommandEncoder::drawPrimitives() for why we do this
	_savedFunctionResources.push_back(_functionResources);
};

// MTLDispatchThreadgroupsIndirectArguments is three 32-bit values, as VkDispatchIndirectCommand is.
static_assert(sizeof(VkDispatchIndirectCommand) == 3 * sizeof(uint32_t), "indirect dispatch arguments are three 32-bit values");

void Indium::PrivateComputeCommandEncoder::dispatchThreadgroups(const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset, Size threadsPerThreadgroup) {
	if (!_pso || !indirectArgumentsFit(indirectBuffer, indirectBufferOffset, sizeof(VkDispatchIndirectCommand))) {
		return;
	}

	auto buf = _privateCommandBuffer.lock();

	auto pipeline = _pso->createPipeline(threadsPerThreadgroup, _threadgroupMemoryLengths, std::nullopt, _stageInRegion);
	_savedPipelines.push_back(pipeline);

	DynamicVK::vkCmdBindPipeline(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	updateBindings();

	// the arguments may have been written by the encoders before, or by an earlier dispatch of this one
	orderDispatch(indirectBuffer, indirectBufferOffset);

	// the buffer has to stay alive until the command buffer has completed
	_keepAliveBuffers.push_back(indirectBuffer);

	beginTimedDispatch(buf->commandBuffer());
	DynamicVK::vkCmdDispatchIndirect(buf->commandBuffer(), std::dynamic_pointer_cast<PrivateBuffer>(indirectBuffer)->buffer(), indirectBufferOffset + std::dynamic_pointer_cast<PrivateBuffer>(indirectBuffer)->bufferOffset());
	endTimedDispatch(buf->commandBuffer());

	// see PrivateRenderCommandEncoder::drawPrimitives() for why we do this
	_savedFunctionResources.push_back(_functionResources);
};

void Indium::PrivateComputeCommandEncoder::dispatchThreads(Size threadsPerGrid, Size threadsPerThreadgroup) {
	if (!_pso || threadsPerThreadgroup.width == 0 || threadsPerThreadgroup.height == 0 || threadsPerThreadgroup.depth == 0) {
		return;
	}

	// whole threadgroups that cover the grid
	auto roundedUp = [](size_t threads, size_t perGroup) { return threads / perGroup + (threads % perGroup != 0); };
	Size threadgroupsPerGrid {
		roundedUp(threadsPerGrid.width, threadsPerThreadgroup.width),
		roundedUp(threadsPerGrid.height, threadsPerThreadgroup.height),
		roundedUp(threadsPerGrid.depth, threadsPerThreadgroup.depth),
	};

	bool partial =
		(threadgroupsPerGrid.width * threadsPerThreadgroup.width != threadsPerGrid.width) ||
		(threadgroupsPerGrid.height * threadsPerThreadgroup.height != threadsPerGrid.height) ||
		(threadgroupsPerGrid.depth * threadsPerThreadgroup.depth != threadsPerGrid.depth);

	if (!partial) {
		return dispatchThreadgroups(threadgroupsPerGrid, threadsPerThreadgroup);
	}

	// Vulkan only dispatches whole threadgroups, so the threads past the end of the grid run too, unless the
	// kernel skips them. Running them anyway would write past what the application made room for.
	if (!_pso->supportsPartialThreadgroups()) {
		throw std::runtime_error("This function cannot be dispatched over a grid that is not a multiple of the threadgroup size: it has no way to skip the threads beyond the grid");
	}
	// In the threadgroups that the grid edge cuts, [[threads_per_threadgroup]] is the
	// size the dispatch was given, not the size of the cut threadgroup.
	return dispatchThreadgroups(threadgroupsPerGrid, threadsPerThreadgroup, threadsPerGrid);
};

void Indium::PrivateComputeCommandEncoder::setImageblockSize(size_t width, size_t height) {
	throw std::runtime_error("TODO");
};

void Indium::PrivateComputeCommandEncoder::setStageInRegion(Region region) {
	// the kernels read it from the specialization constants of the pipelines the dispatches create
	_stageInRegion = region;
};

Indium::DispatchType Indium::PrivateComputeCommandEncoder::dispatchType() {
	return _descriptor.dispatchType;
};

void Indium::PrivateComputeCommandEncoder::updateBindings() {
	auto buf = _privateCommandBuffer.lock();

	std::array<VkDescriptorSet, 1> descriptorSets = createDescriptorSets(_pso->descriptorSetLayouts().layouts, _descriptorPools, _privateDevice, { _functionResources }, { _pso->functionInfo() }, _keepAliveBuffers, _scratch);

	DynamicVK::vkCmdBindDescriptorSets(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _pso->layout(), 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
};

void Indium::PrivateComputeCommandEncoder::recordUntrackedBarrier() {
	auto buf = _privateCommandBuffer.lock();
	VkMemoryBarrier2 barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
	HazardTracker::record(buf->commandBuffer(), { barrier });
};

void Indium::PrivateComputeCommandEncoder::orderDispatch(const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset) {
	auto buf = _privateCommandBuffer.lock();
	auto vkCommandBuffer = buf->commandBuffer();

	if (!_tracked) {
		// Every encoder ends with a barrier on everything, but the dispatches of one encoder are not ordered by that. A
		// serial encoder runs them one after the other, so each waits for the one before it; one of a
		// concurrent encoder only has to wait for the dispatch that wrote its arguments.
		if ((_descriptor.dispatchType == DispatchType::Serial && _dispatchCount > 0) || indirectBuffer) {
			recordUntrackedBarrier();
		}
		++_dispatchCount;
		return;
	}

	auto& tracker = buf->hazards();
	constexpr VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

	bool untracked = forEachBoundResource(_functionResources, _pso->functionInfo(), [&](const HazardRange& range, VkAccessFlags2 access) {
		tracker.use(range, stage, access);
	});
	_declared.applyTo(tracker);
	if (auto privateBuffer = std::dynamic_pointer_cast<PrivateBuffer>(indirectBuffer)) {
		if (privateBuffer->hazardTrackingMode() == HazardTrackingMode::HazardTrackingModeUntracked) {
			untracked = true;
		} else {
			tracker.use(privateBuffer->hazardRange(indirectBufferOffset, sizeof(VkDispatchIndirectCommand)), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
		}
	}
	tracker.flush(vkCommandBuffer);
	_usesUntracked = _usesUntracked || untracked;

	// The dispatches of a serial encoder run one after the other, whichever memory they share, so one that
	// reaches a resource nothing tracks (which the tracker cannot order) waits for the ones before it.
	if (untracked && _descriptor.dispatchType == DispatchType::Serial && _dispatchCount > 0) {
		recordUntrackedBarrier();
	}
	++_dispatchCount;
};

void Indium::PrivateComputeCommandEncoder::useResource(std::shared_ptr<Resource> resource, ResourceUsage usage) {
	if (_tracked && !addDeclaredResource(_declared, resource, usage, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)) {
		_usesUntracked = true;
	}
};

void Indium::PrivateComputeCommandEncoder::useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage) {
	for (const auto& resource: resources) {
		useResource(resource, usage);
	}
};

void Indium::PrivateComputeCommandEncoder::useHeap(const std::shared_ptr<Heap>& heap) {
	if (_tracked && !addDeclaredHeap(_declared, heap, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)) {
		_usesUntracked = true;
	}
};

void Indium::PrivateComputeCommandEncoder::useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps) {
	for (const auto& heap: heaps) {
		useHeap(heap);
	}
};

void Indium::PrivateComputeCommandEncoder::memoryBarrier(BarrierScope scope) {
	constexpr size_t knownScope = static_cast<size_t>(BarrierScope::Buffers | BarrierScope::Textures | BarrierScope::RenderTargets);
	if (!scope || (static_cast<size_t>(scope) & ~knownScope)) {
		throw std::invalid_argument("Invalid memory barrier scope");
	}
	auto buf = _privateCommandBuffer.lock();

	// The resources the dispatches used are ordered by the tracker; the ones it does not know (untracked, or
	// reached through memory it cannot see into) by a barrier between all the dispatches.
	if (_usesUntracked || !_tracked) {
		recordUntrackedBarrier();
	}
	if (_tracked) {
		buf->hazards().groupBarrier(buf->commandBuffer(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
			VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
	}
};

void Indium::PrivateComputeCommandEncoder::memoryBarrier(const std::vector<std::shared_ptr<Resource>>& resources) {
	// ordering the given resources only is not cheaper than ordering all of them
	memoryBarrier(BarrierScope::Buffers | BarrierScope::Textures);
};

void Indium::PrivateComputeCommandEncoder::sampleCounters(const std::shared_ptr<CounterSampleBuffer>& sampleBuffer, size_t sampleIndex, bool barrier) {
	if (auto commandBuffer = _privateCommandBuffer.lock()) {
		// the sample is taken once the commands before it have finished, whether or not a barrier was asked for
		PrivateCounterSampleBuffer::record(sampleBuffer, commandBuffer->commandBuffer(), sampleIndex, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	}
};
