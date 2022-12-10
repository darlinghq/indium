#include <indium/compute-pipeline.private.hpp>
#include <indium/device.private.hpp>
#include <stdexcept>

Indium::ComputePipelineState::~ComputePipelineState() {};

Indium::PrivateComputePipelineState::PrivateComputePipelineState(std::shared_ptr<PrivateDevice> device, const ComputePipelineDescriptor& descriptor):
	_privateDevice(device),
	_descriptor(descriptor),
	_descriptorSetLayouts(_privateDevice)
{
	if (_descriptor.stageInputDescriptor) {
		// TODO
		//
		// Vulkan doesn't support stage-in/vertex-buffer parameters in compute shaders,
		// so we'll have to emulate it by passing the binding info to the shader and
		// having it compute the right addresses on its own
		throw std::runtime_error("TODO: support stage-in parameters in compute shaders");
	}

	_descriptorSetLayouts.processFunction(std::dynamic_pointer_cast<PrivateFunction>(_descriptor.computeFunction), 0);

	VkPipelineLayoutCreateInfo layoutInfo {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = _descriptorSetLayouts.layouts.size();
	layoutInfo.pSetLayouts = _descriptorSetLayouts.layouts.data();

	if (vkCreatePipelineLayout(_privateDevice->device(), &layoutInfo, nullptr, &_layout) != VK_SUCCESS) {
		// TODO
		abort();
	}

	// determine device properties
	VkPhysicalDeviceVulkan13Properties vk13Props {};
	vk13Props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
	VkPhysicalDeviceProperties2 props {};
	props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props.pNext = &vk13Props;
	vkGetPhysicalDeviceProperties2(_privateDevice->physicalDevice(), &props);

	_maxTotalThreadsPerThreadgroup = vk13Props.maxSubgroupSize;
	_threadExecutionWidth = props.properties.limits.maxComputeWorkGroupInvocations;
	_staticThreadgroupMemoryLength = props.properties.limits.maxComputeSharedMemorySize; // not entirely sure about this one, but it seems close enough
};

Indium::PrivateComputePipelineState::~PrivateComputePipelineState() {
	if (_layout) {
		vkDestroyPipelineLayout(_privateDevice->device(), _layout, nullptr);
	}
};

VkPipeline Indium::PrivateComputePipelineState::createPipeline(Size threadsPerThreadgroup) {
	// TODO: optimize this by using pipeline caches.
	//       also, we can store the most recent pipeline and, if the number of threads per threadgroup stays the same,
	//       we can just return that instead (wrapped in a reference counting object).

	VkPipeline pipeline = VK_NULL_HANDLE;

	auto func = std::dynamic_pointer_cast<PrivateFunction>(_descriptor.computeFunction);
	auto funcName = func->name();

	std::array<uint32_t, 3> threadsPerThreadgroupData = { static_cast<uint32_t>(threadsPerThreadgroup.width), static_cast<uint32_t>(threadsPerThreadgroup.height), static_cast<uint32_t>(threadsPerThreadgroup.depth) };

	std::array<VkSpecializationMapEntry, 3> mapEntries {};

	for (size_t i = 0; i < mapEntries.size(); ++i) {
		mapEntries[i].constantID = i;
		mapEntries[i].offset = i * sizeof(uint32_t);
		mapEntries[i].size = sizeof(uint32_t);
	}

	VkSpecializationInfo specInfo {};
	specInfo.mapEntryCount = mapEntries.size();
	specInfo.pMapEntries = mapEntries.data();
	specInfo.dataSize = threadsPerThreadgroupData.size() * sizeof(*threadsPerThreadgroupData.data());
	specInfo.pData = threadsPerThreadgroupData.data();

	VkComputePipelineCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	info.stage.module = func->library()->shaderModule();
	info.stage.pName = funcName.c_str();
	info.stage.pSpecializationInfo = &specInfo;
	info.layout = _layout;

	if (vkCreateComputePipelines(_privateDevice->device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
		// TODO
		abort();
	}

	return pipeline;
};

const Indium::FunctionInfo& Indium::PrivateComputePipelineState::functionInfo() const {
	return std::dynamic_pointer_cast<PrivateFunction>(_descriptor.computeFunction)->functionInfo();
};

std::shared_ptr<Indium::Device> Indium::PrivateComputePipelineState::device() {
	return _privateDevice;
};

size_t Indium::PrivateComputePipelineState::imageblockMemoryLength(Size dimensions) {
	throw std::runtime_error("TODO");
};

std::shared_ptr<Indium::FunctionHandle> Indium::PrivateComputePipelineState::functionHandle(std::shared_ptr<Function> function) {
	throw std::runtime_error("TODO");
};

std::shared_ptr<Indium::ComputePipelineState> Indium::PrivateComputePipelineState::newComputePipelineState(const std::vector<std::shared_ptr<Function>>& functions) {
	throw std::runtime_error("TODO");
};

std::shared_ptr<Indium::VisibleFunctionTable> Indium::PrivateComputePipelineState::newVisibleFunctionTable(const VisibleFunctionTableDescriptor& descriptor) {
	throw std::runtime_error("TODO");
};

std::shared_ptr<Indium::IntersectionFunctionTable> Indium::PrivateComputePipelineState::newIntersectionFunctionTable(const IntersectionFunctionTableDescriptor& descriptor) {
	throw std::runtime_error("TODO");
};
