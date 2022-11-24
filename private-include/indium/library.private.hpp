#pragma once

#include <indium/library.hpp>

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <string>
#include <vector>

#include <indium/types.hpp>

namespace Indium {
	class PrivateLibrary;
	class PrivateDevice;

	enum class BindingType {
		StageIn,
		Buffer,
	};

	struct BindingInfo {
		BindingType type;
		size_t index;
	};

	struct FunctionInfo {
		FunctionType functionType;
		// note that these bindings use the same set of indices between different resource types (e.g. stage-ins, buffers, and textures)
		// like Vulkan does whereas Metal uses per-resource-type indexing.
		// the Metal-to-SPIR-V translator should output code that follows this indexing scheme.
		// in this indexing scheme, all buffers are bound first, followed by stage-ins, followed by textures, followed by samplers.
		std::vector<BindingInfo> bindings;
	};

	class PrivateFunction: public Function {
	public:
		PrivateFunction(std::shared_ptr<PrivateLibrary> library, const std::string& name, const FunctionInfo& functionInfo);

		virtual std::shared_ptr<Device> device() override;

		INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);

		INDIUM_PROPERTY_OBJECT(PrivateLibrary, l, L,ibrary);
		INDIUM_PROPERTY_READONLY(const FunctionInfo&, f, F,unctionInfo);
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
		virtual std::shared_ptr<Device> device() override;

		INDIUM_PROPERTY_READONLY_OBJECT(PrivateDevice, p, P,rivateDevice);

		INDIUM_PROPERTY(VkShaderModule, s, S,haderModule) = nullptr;
	};
};
