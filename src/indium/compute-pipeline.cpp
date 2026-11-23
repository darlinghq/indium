#include <indium/compute-pipeline.private.hpp>
#include <indium/device.private.hpp>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <indium/dynamic-vk.hpp>

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

	if (DynamicVK::vkCreatePipelineLayout(_privateDevice->device(), &layoutInfo, nullptr, &_layout) != VK_SUCCESS) {
		// TODO
		abort();
	}

	VkPhysicalDeviceVulkan11Properties vk11Props {};
	vk11Props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
	VkPhysicalDeviceProperties2 props {};
	props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props.pNext = &vk11Props;
	DynamicVK::vkGetPhysicalDeviceProperties2(_privateDevice->physicalDevice(), &props);

	// 1024 is Metal's limit
	_maxTotalThreadsPerThreadgroup = std::min<size_t>(props.properties.limits.maxComputeWorkGroupInvocations, 1024);
	if (_descriptor.maxTotalThreadsPerThreadgroup) {
		_maxTotalThreadsPerThreadgroup = std::min(_maxTotalThreadsPerThreadgroup, _descriptor.maxTotalThreadsPerThreadgroup);
	}
	// a SPIR-V 1.5 module, created without a required subgroup size or stage flags, runs at the default size
	_threadExecutionWidth = vk11Props.subgroupSize;
	// Iridium does not report the threadgroup memory a kernel declares, so _staticThreadgroupMemoryLength stays 0
};

Indium::PrivateComputePipelineState::~PrivateComputePipelineState() {
	if (_layout) {
		DynamicVK::vkDestroyPipelineLayout(_privateDevice->device(), _layout, nullptr);
	}
};

VkPipeline Indium::PrivateComputePipelineState::createPipeline(Size threadsPerThreadgroup, const std::optional<Size>& gridSize) {
	// The device's pipeline cache makes repeating a creation cheap, within the process and across runs.
	// Keeping the most recent pipeline for an unchanged threadgroup size would save even that lookup.

	VkPipeline pipeline = VK_NULL_HANDLE;

	auto func = std::dynamic_pointer_cast<PrivateFunction>(_descriptor.computeFunction);
	auto funcName = func->name();

	// SpecIds 0 to 2 are the threadgroup size; the function's own function constants follow
	std::vector<uint32_t> specializationData = { static_cast<uint32_t>(threadsPerThreadgroup.width), static_cast<uint32_t>(threadsPerThreadgroup.height), static_cast<uint32_t>(threadsPerThreadgroup.depth) };
	std::vector<VkSpecializationMapEntry> mapEntries(3);

	for (size_t i = 0; i < mapEntries.size(); ++i) {
		mapEntries[i].constantID = i;
		mapEntries[i].offset = i * sizeof(uint32_t);
		mapEntries[i].size = sizeof(uint32_t);
	}

	if (gridSize) {
		auto ids = func->functionInfo().gridSizeSpecializationIDs;
		if (!ids) {
			throw std::runtime_error("The function cannot skip the threads beyond a grid size");
		}
		const size_t dimensions[3] = { gridSize->width, gridSize->height, gridSize->depth };
		for (size_t i = 0; i < 3; ++i) {
			VkSpecializationMapEntry entry {};
			entry.constantID = (*ids)[i];
			entry.offset = specializationData.size() * sizeof(uint32_t);
			entry.size = sizeof(uint32_t);
			mapEntries.push_back(entry);
			// a dimension that does not fit in 32 bits cannot exclude any thread of a Vulkan dispatch
			specializationData.push_back(static_cast<uint32_t>(std::min<size_t>(dimensions[i], UINT32_MAX)));
		}
	}

	if (auto functionSpecialization = func->specializationInfo()) {
		auto base = specializationData.size() * sizeof(uint32_t);
		auto entries = reinterpret_cast<const VkSpecializationMapEntry*>(functionSpecialization->pMapEntries);
		for (uint32_t i = 0; i < functionSpecialization->mapEntryCount; ++i) {
			auto entry = entries[i];
			entry.offset += base;
			mapEntries.push_back(entry);
		}
		auto words = static_cast<const uint32_t*>(functionSpecialization->pData);
		specializationData.insert(specializationData.end(), words, words + functionSpecialization->dataSize / sizeof(uint32_t));
	}

	VkSpecializationInfo specInfo {};
	specInfo.mapEntryCount = mapEntries.size();
	specInfo.pMapEntries = mapEntries.data();
	specInfo.dataSize = specializationData.size() * sizeof(uint32_t);
	specInfo.pData = specializationData.data();

	VkComputePipelineCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	info.stage.module = func->library()->shaderModule();
	info.stage.pName = funcName.c_str();
	info.stage.pSpecializationInfo = &specInfo;
	info.layout = _layout;

	if (_privateDevice->createComputePipeline(info, &pipeline) != VK_SUCCESS) {
		// TODO
		abort();
	}

	return pipeline;
};

bool Indium::PrivateComputePipelineState::supportsPartialThreadgroups() const {
	return functionInfo().gridSizeSpecializationIDs.has_value();
};

std::string Indium::PrivateComputePipelineState::timingName() const {
	return "compute " + _descriptor.computeFunction->name();
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
