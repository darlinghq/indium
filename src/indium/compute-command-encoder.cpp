#include <indium/compute-command-encoder.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

Indium::ComputeCommandEncoder::~ComputeCommandEncoder() {};

Indium::PrivateComputeCommandEncoder::PrivateComputeCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const ComputePassDescriptor& descriptor):
	_privateCommandBuffer(commandBuffer),
	_privateDevice(std::dynamic_pointer_cast<PrivateDevice>(commandBuffer->device())),
	_descriptor(descriptor)
{
	VkDescriptorPoolCreateInfo poolCreateInfo {};
	poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolCreateInfo.poolSizeCount = poolSizes.size();
	poolCreateInfo.pPoolSizes = poolSizes.data();
	poolCreateInfo.maxSets = 64;

	if (DynamicVK::vkCreateDescriptorPool(_privateDevice->device(), &poolCreateInfo, nullptr, &_pool) != VK_SUCCESS) {
		// TODO
		abort();
	}
};

Indium::PrivateComputeCommandEncoder::~PrivateComputeCommandEncoder() {
	for (auto& pipeline: _savedPipelines) {
		DynamicVK::vkDestroyPipeline(_privateDevice->device(), pipeline, nullptr);
	}
	DynamicVK::vkDestroyDescriptorPool(_privateDevice->device(), _pool, 0);
};

void Indium::PrivateComputeCommandEncoder::endEncoding() {
	// nothing for now
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
	_functionResources.setBytes(_privateDevice, bytes, length, index);
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

	std::array<VkDescriptorSet, 1> descriptorSets = createDescriptorSets(_pso->descriptorSetLayouts().layouts, _pool, _privateDevice, { _functionResources }, { _pso->functionInfo() }, _keepAliveBuffers);

	DynamicVK::vkCmdBindDescriptorSets(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _pso->layout(), 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
};
