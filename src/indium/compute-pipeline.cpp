#include <indium/compute-pipeline.private.hpp>
#include <indium/device.private.hpp>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <indium/dynamic-vk.hpp>

Indium::ComputePipelineState::~ComputePipelineState() {};

// the width of the SIMD-groups of Apple GPUs, which Metal code relies on
static constexpr uint32_t simdgroupWidth = 32;

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

	VkPhysicalDeviceVulkan13Properties vk13Props {};
	vk13Props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
	VkPhysicalDeviceVulkan11Properties vk11Props {};
	vk11Props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
	vk11Props.pNext = &vk13Props;
	VkPhysicalDeviceProperties2 props {};
	props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props.pNext = &vk11Props;
	DynamicVK::vkGetPhysicalDeviceProperties2(_privateDevice->physicalDevice(), &props);

	// Run at simdgroupWidth where the device can require it for every threadgroup size it allows, up to Metal's 1024 threads.
	// Vulkan 1.3 has subgroupSizeControl, which createLogicalDevice() enables.
	_requireSimdgroupWidth = vk13Props.minSubgroupSize <= simdgroupWidth && simdgroupWidth <= vk13Props.maxSubgroupSize &&
		(vk13Props.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
		uint64_t { simdgroupWidth } * vk13Props.maxComputeWorkgroupSubgroups >= std::min<uint32_t>(props.properties.limits.maxComputeWorkGroupInvocations, 1024);

	// 1024 is Metal's limit
	const size_t deviceLimit = std::min<size_t>(props.properties.limits.maxComputeWorkGroupInvocations, 1024);
	_maxTotalThreadsPerThreadgroup = deviceLimit;
	if (_descriptor.maxTotalThreadsPerThreadgroup) {
		_maxTotalThreadsPerThreadgroup = std::min(_maxTotalThreadsPerThreadgroup, _descriptor.maxTotalThreadsPerThreadgroup);
	}
	if (auto functionLimit = functionInfo().maxTotalThreadsPerThreadgroup) {
		// the Metal Shading Language specification has no pipeline for a kernel that allows more threads than the device
		if (functionLimit > deviceLimit) {
			throw std::invalid_argument("The function allows " + std::to_string(functionLimit) + " threads per threadgroup, more than the device's " + std::to_string(deviceLimit));
		}
		_maxTotalThreadsPerThreadgroup = std::min(_maxTotalThreadsPerThreadgroup, functionLimit);
	}

	// with a required size, that size is the only one a dispatch can use
	const auto& required = _descriptor.requiredThreadsPerThreadgroup;
	if (required.width || required.height || required.depth) {
		const auto& limits = props.properties.limits;
		if (!required.width || !required.height || !required.depth ||
			required.width > limits.maxComputeWorkGroupSize[0] || required.height > limits.maxComputeWorkGroupSize[1] || required.depth > limits.maxComputeWorkGroupSize[2] ||
			required.width * required.height * required.depth > _maxTotalThreadsPerThreadgroup) {
			throw std::invalid_argument("The required threadgroup size " + std::to_string(required.width) + "x" + std::to_string(required.height) + "x" + std::to_string(required.depth) +
				" is not one the pipeline allows: at most " + std::to_string(_maxTotalThreadsPerThreadgroup) + " threads, and at most " +
				std::to_string(limits.maxComputeWorkGroupSize[0]) + "x" + std::to_string(limits.maxComputeWorkGroupSize[1]) + "x" + std::to_string(limits.maxComputeWorkGroupSize[2]));
		}
		_requiredThreadsPerThreadgroup = required;
		_maxTotalThreadsPerThreadgroup = required.width * required.height * required.depth;
	}
	// where the width cannot be required, a SPIR-V 1.5 module, created without a required subgroup size or stage flags,
	// runs at the default size
	_threadExecutionWidth = _requireSimdgroupWidth ? simdgroupWidth : vk11Props.subgroupSize;
	// Iridium does not report the threadgroup memory a kernel declares, so _staticThreadgroupMemoryLength stays 0

	_descriptorSetLayouts.processFunction(std::dynamic_pointer_cast<PrivateFunction>(_descriptor.computeFunction), 0);

	VkPipelineLayoutCreateInfo layoutInfo {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = _descriptorSetLayouts.layouts.size();
	layoutInfo.pSetLayouts = _descriptorSetLayouts.layouts.data();

	if (DynamicVK::vkCreatePipelineLayout(_privateDevice->device(), &layoutInfo, nullptr, &_layout) != VK_SUCCESS) {
		// TODO
		abort();
	}
};

Indium::PrivateComputePipelineState::~PrivateComputePipelineState() {
	if (_layout) {
		DynamicVK::vkDestroyPipelineLayout(_privateDevice->device(), _layout, nullptr);
	}
};

VkPipeline Indium::PrivateComputePipelineState::createPipeline(Size threadsPerThreadgroup, const std::unordered_map<size_t, size_t>& threadgroupMemoryLengths, const std::optional<Size>& gridSize, const Region& stageInRegion) {
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

	for (const auto& argument: func->functionInfo().threadgroupArguments) {
		auto length = threadgroupMemoryLengths.find(argument.index);
		size_t elements = length == threadgroupMemoryLengths.end() ? 0 : length->second / argument.elementSize;
		VkSpecializationMapEntry entry {};
		entry.constantID = argument.specializationID;
		entry.offset = specializationData.size() * sizeof(uint32_t);
		entry.size = sizeof(uint32_t);
		mapEntries.push_back(entry);
		// Vulkan cannot declare an array of zero elements
		specializationData.push_back(static_cast<uint32_t>(std::clamp<size_t>(elements, 1, UINT32_MAX)));
	}

	if (auto ids = func->functionInfo().stageInRegionSpecializationIDs) {
		const size_t values[6] = { stageInRegion.origin.x, stageInRegion.origin.y, stageInRegion.origin.z, stageInRegion.size.width, stageInRegion.size.height, stageInRegion.size.depth };
		for (size_t i = 0; i < 6; ++i) {
			VkSpecializationMapEntry entry {};
			entry.constantID = (*ids)[i];
			entry.offset = specializationData.size() * sizeof(uint32_t);
			entry.size = sizeof(uint32_t);
			mapEntries.push_back(entry);
			// the kernel reads 32-bit values
			specializationData.push_back(static_cast<uint32_t>(std::min<size_t>(values[i], UINT32_MAX)));
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

	VkPipelineShaderStageRequiredSubgroupSizeCreateInfo requiredSubgroupSize {};
	if (_requireSimdgroupWidth) {
		requiredSubgroupSize.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
		requiredSubgroupSize.requiredSubgroupSize = simdgroupWidth;
		info.stage.pNext = &requiredSubgroupSize;
		// Without the flag, a SPIR-V 1.5 module may get partial subgroups anywhere; Metal's SIMD-groups are full but
		// for a threadgroup's last. The flag needs the threadgroup's X size to be a multiple of the width.
		if (threadsPerThreadgroup.width % simdgroupWidth == 0) {
			info.stage.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
		}
	}

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
