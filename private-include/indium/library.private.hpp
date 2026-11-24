#pragma once

#include <array>
#include <optional>

#include <indium/library.hpp>

#include <vulkan/vulkan.h>

#include <map>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>

#include <indium/types.hpp>

namespace Indium {
	class PrivateLibrary;
	class PrivateDevice;
	class SamplerState;

	/** The Vulkan descriptor type a texture binding is written as. */
	inline VkDescriptorType textureDescriptorType(const BindingDescriptor& binding) {
		bool storage = binding.textureAccessType != TextureAccessType::Sample;
		if (binding.texelBuffer) {
			return storage ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		}
		return storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	};

	struct FunctionInfo {
		FunctionType functionType;
		// note that these bindings use the same set of indices between different resource types (e.g. stage-ins, buffers, and textures)
		// like Vulkan does whereas Metal uses per-resource-type indexing.
		// the Metal-to-SPIR-V translator should output code that follows this indexing scheme.
		// in this indexing scheme, all buffers are bound first, followed by stage-ins, followed by textures, followed by samplers.
		std::vector<BindingDescriptor> bindings;
		std::vector<EmbeddedSamplerDescriptor> embeddedSamplers;
		std::vector<std::shared_ptr<SamplerState>> embeddedSamplerStates;
		std::vector<ArgumentInfo> arguments;
		std::vector<FunctionConstantInfo> functionConstants;
		// see FunctionReflection::stageInterface
		std::vector<StageInterfaceField> stageInterface;
		// see FunctionReflection::gridSizeSpecializationIDs
		std::optional<std::array<uint32_t, 3>> gridSizeSpecializationIDs;
		// see FunctionReflection::threadgroupArguments
		std::vector<ThreadgroupArgumentInfo> threadgroupArguments;
		// see FunctionReflection::maxTotalThreadsPerThreadgroup
		size_t maxTotalThreadsPerThreadgroup = 0;
	};

	class PrivateFunction: public Function {
	public:
		PrivateFunction(std::shared_ptr<PrivateLibrary> library, const std::string& name, const FunctionInfo& functionInfo);

		virtual std::shared_ptr<Device> device() override;
		virtual FunctionType functionType() const override;
		virtual const std::vector<ArgumentInfo>& argumentInfo() const override;
		virtual const std::vector<FunctionConstantInfo>& functionConstants() const override;

		/**
		 * Gives the function the values of its function constants. Every declared constant gets its
		 * value (zero when it was given none) and its defined flag.
		 *
		 * @returns false, with `errorMessage` set, if a value names no constant or has the wrong type.
		 */
		bool specialize(const FunctionConstantValues& values, std::string& errorMessage);

		/**
		 * The specialization constants to create a pipeline stage with, or nullptr when the function
		 * declares none. Valid for the lifetime of the function.
		 */
		const VkSpecializationInfo* specializationInfo() const;

		INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);

		INDIUM_PROPERTY_OBJECT(PrivateLibrary, l, L,ibrary);
		INDIUM_PROPERTY_READONLY(const FunctionInfo&, f, F,unctionInfo);

	private:
		void addSpecialization(uint32_t id, FunctionConstantType type, uint64_t bits);

		std::vector<VkSpecializationMapEntry> _specializationEntries;
		std::vector<uint32_t> _specializationData;
		VkSpecializationInfo _specializationInfo {};
	};

	class PrivateLibrary: public Library, public std::enable_shared_from_this<PrivateLibrary> {
	public:
		using FunctionInfoMap = std::unordered_map<std::string, FunctionInfo>;

	private:
		FunctionInfoMap _functionInfos;

	public:
		/**
		 * @param data A buffer containing the SPIR-V bytecode for the library.
		 * @param functionInfos A map containing function information for each function in the library, keyed by function name.
		 */
		PrivateLibrary(std::shared_ptr<PrivateDevice> device, const char* data, size_t dataLength, FunctionInfoMap functionInfos);
		~PrivateLibrary();

		virtual std::shared_ptr<Function> newFunction(const std::string& name) override;
		virtual std::shared_ptr<Function> newFunction(const std::string& name, const FunctionConstantValues& values, std::string* errorMessage = nullptr) override;
		virtual std::shared_ptr<Device> device() override;

		INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);

		INDIUM_PROPERTY(VkShaderModule, s, S,haderModule) = VK_NULL_HANDLE;

public:
		/**
		 * The module to use for the fragment function `entry` of this library in a pipeline whose vertex
		 * function returns `vertexOutputs`: the library's own module when the fragment function's inputs
		 * already are at the Locations of the outputs with their names, and otherwise a copy of the module
		 * in which they are. Lives as long as the library. Returns VK_NULL_HANDLE if the copy cannot be made.
		 */
		VkShaderModule fragmentModuleLinkedTo(const std::string& entry, const std::vector<StageInterfaceField>& vertexOutputs);

	private:
		// the module's words, kept when a function has varyings to link by name
		std::vector<uint32_t> _words;
		std::mutex _linkedMutex;
		std::map<std::pair<std::string, std::vector<uint32_t>>, VkShaderModule> _linkedModules;
	};
};
