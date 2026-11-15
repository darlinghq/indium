#include <indium/compute-command-encoder.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

Indium::ComputeCommandEncoder::~ComputeCommandEncoder() {};

Indium::PrivateComputeCommandEncoder::PrivateComputeCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const ComputePassDescriptor& descriptor):
	_privateCommandBuffer(commandBuffer),
	_privateDevice(std::dynamic_pointer_cast<PrivateDevice>(commandBuffer->device())),
	_descriptor(descriptor),
	_descriptorPools(_privateDevice->device())
{
};

Indium::PrivateComputeCommandEncoder::~PrivateComputeCommandEncoder() {
	for (auto& pipeline: _savedPipelines) {
		DynamicVK::vkDestroyPipeline(_privateDevice->device(), pipeline, nullptr);
	}
};

void Indium::PrivateComputeCommandEncoder::endEncoding() {
	if (auto commandBuffer = _privateCommandBuffer.lock()) {
		finishTrackedEncoder(commandBuffer->commandBuffer());
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
	// The length sizes the kernel argument in the threadgroup address space at `index`. The kernels that exist
	// here keep their threadgroup memory in variables of their own (a threadgroup argument is not lowered by the
	// shader compilers yet), so there is no argument to size; the length is recorded and nothing reads it.
	_threadgroupMemoryLengths[index] = length;
};

void Indium::PrivateComputeCommandEncoder::dispatchThreadgroups(Size threadgroupsPerGrid, Size threadsPerThreadgroup) {
	dispatchThreadgroups(threadgroupsPerGrid, threadsPerThreadgroup, std::nullopt);
};

void Indium::PrivateComputeCommandEncoder::dispatchThreadgroups(Size threadgroupsPerGrid, Size threadsPerThreadgroup, const std::optional<Size>& gridSize) {
	auto buf = _privateCommandBuffer.lock();

	auto pipeline = _pso->createPipeline(threadsPerThreadgroup, gridSize);
	_savedPipelines.push_back(pipeline);

	DynamicVK::vkCmdBindPipeline(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	// TODO: avoid re-binding descriptors on every dispatch
	updateBindings();

	DynamicVK::vkCmdDispatch(buf->commandBuffer(), threadgroupsPerGrid.width, threadgroupsPerGrid.height, threadgroupsPerGrid.depth);

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
	throw std::runtime_error("TODO");
};

Indium::DispatchType Indium::PrivateComputeCommandEncoder::dispatchType() {
	return _descriptor.dispatchType;
};

void Indium::PrivateComputeCommandEncoder::updateBindings() {
	auto buf = _privateCommandBuffer.lock();

	std::array<VkDescriptorSet, 1> descriptorSets = createDescriptorSets(_pso->descriptorSetLayouts().layouts, _descriptorPools, _privateDevice, { _functionResources }, { _pso->functionInfo() }, _keepAliveBuffers, _scratch);

	DynamicVK::vkCmdBindDescriptorSets(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _pso->layout(), 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
};
