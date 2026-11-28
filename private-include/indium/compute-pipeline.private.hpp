#pragma once

#include <indium/compute-pipeline.hpp>
#include <indium/pipeline.private.hpp>

namespace Indium {
	class PrivateDevice;

	class PrivateComputePipelineState: public ComputePipelineState {
	private:
		std::shared_ptr<PrivateDevice> _privateDevice;
		ComputePipelineDescriptor _descriptor;
		bool _requireSimdgroupWidth = false;

	public:
		PrivateComputePipelineState(std::shared_ptr<PrivateDevice> device, const ComputePipelineDescriptor& descriptor);
		~PrivateComputePipelineState();

		// the pipeline unfortunately has to be recreated on every dispatch since Metal allows setting the number of threads-per-threadgroup
		// at dispatch-time in the API while Vulkan only allows setting it within shader code, which usually means it has to be baked-in at
		// shader compilation time. however, Vulkan *does* allow it to be set with a specialization constant within the shader, which means
		// that we can set it at pipeline-creation time. thus, we have to create a new pipeline with an updated set of specialization constants
		// when dispatching the work.
		//
		// The function's threadgroup memory arguments (FunctionReflection::threadgroupArguments) get the lengths in
		// `threadgroupMemoryLengths`, by index; an argument without one gets a single element.
		//
		// With `gridSize`, the function's grid size constants (FunctionReflection::gridSizeSpecializationIDs) are
		// set to it, so the kernel skips the threads beyond it. The function must have them. A function that reads the
		// stage-in region gets `stageInRegion`.
		VkPipeline createPipeline(Size threadsPerThreadgroup, const std::unordered_map<size_t, size_t>& threadgroupMemoryLengths, const std::optional<Size>& gridSize = std::nullopt, const Region& stageInRegion = {});

		/** Whether the function can be dispatched over a grid that is not a multiple of the threadgroup size. */
		bool supportsPartialThreadgroups() const;

		/** "compute <kernel>", the name pipeline timing reports. */
		std::string timingName() const;

		const FunctionInfo& functionInfo() const;

		virtual std::shared_ptr<Device> device() override;
		virtual size_t imageblockMemoryLength(Size dimensions) override;
		virtual std::shared_ptr<FunctionHandle> functionHandle(std::shared_ptr<Function> function) override;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(const std::vector<std::shared_ptr<Function>>& functions) override;
		virtual std::shared_ptr<VisibleFunctionTable> newVisibleFunctionTable(const VisibleFunctionTableDescriptor& descriptor) override;
		virtual std::shared_ptr<IntersectionFunctionTable> newIntersectionFunctionTable(const IntersectionFunctionTableDescriptor& descriptor) override;

		INDIUM_PROPERTY_REF(DescriptorSetLayouts<1>, d,D,escriptorSetLayouts);
		INDIUM_PROPERTY_READONLY(VkPipelineLayout, l,L,ayout) = VK_NULL_HANDLE;
	};
};
