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
		commandBuffer->endEncoderSegment(_functionResources.usedTextures, {});
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
	throw std::runtime_error("TODO");
};

void Indium::PrivateComputeCommandEncoder::dispatchThreadgroups(Size threadgroupsPerGrid, Size threadsPerThreadgroup) {
	auto buf = _privateCommandBuffer.lock();

	auto pipeline = _pso->createPipeline(threadsPerThreadgroup);
	_savedPipelines.push_back(pipeline);

	DynamicVK::vkCmdBindPipeline(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	// TODO: avoid re-binding descriptors on every dispatch
	updateBindings();

	DynamicVK::vkCmdDispatch(buf->commandBuffer(), threadgroupsPerGrid.width, threadgroupsPerGrid.height, threadgroupsPerGrid.depth);

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

	auto pipeline = _pso->createPipeline(threadsPerThreadgroup);
	_savedPipelines.push_back(pipeline);

	DynamicVK::vkCmdBindPipeline(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	updateBindings();

	// An earlier dispatch of this encoder may have written the arguments, and Vulkan does not order that
	// against the GPU's read of them the way Metal does for the dispatches of a serial encoder.
	VkMemoryBarrier barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	DynamicVK::vkCmdPipelineBarrier(buf->commandBuffer(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

	// the buffer has to stay alive until the command buffer has completed
	_keepAliveBuffers.push_back(indirectBuffer);

	DynamicVK::vkCmdDispatchIndirect(buf->commandBuffer(), std::dynamic_pointer_cast<PrivateBuffer>(indirectBuffer)->buffer(), indirectBufferOffset);

	// see PrivateRenderCommandEncoder::drawPrimitives() for why we do this
	_savedFunctionResources.push_back(_functionResources);
};

void Indium::PrivateComputeCommandEncoder::dispatchThreads(Size threadsPerGrid, Size threadsPerThreadgroup) {
	Size threadgroupsPerGrid {
		threadsPerGrid.width / threadsPerThreadgroup.width,
		threadsPerGrid.height / threadsPerThreadgroup.height,
		threadsPerGrid.depth / threadsPerThreadgroup.depth,
	};

	if (
		(threadgroupsPerGrid.width * threadsPerThreadgroup.width != threadsPerGrid.width) ||
		(threadgroupsPerGrid.height * threadsPerThreadgroup.height != threadsPerGrid.height) ||
		(threadgroupsPerGrid.depth * threadsPerThreadgroup.depth != threadsPerGrid.depth)
	) {
		throw std::runtime_error("TODO: support partial threadgroups");
	}

	return dispatchThreadgroups(threadgroupsPerGrid, threadsPerThreadgroup);
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
