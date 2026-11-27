#pragma once

#include <indium/library.hpp>

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <string>
#include <vector>

#include <indium/types.hpp>

#include <iridium/iridium.hpp>

#include <cstring>
#include <stdexcept>

namespace Indium {
	namespace detail {
		// SPIR-V binary layout: a five-word header, then instructions whose first word is
		// (word count << 16) | opcode.
		constexpr uint32_t kSpirvHeaderWords = 5;
		constexpr uint32_t kOpCapability = 17;
		constexpr uint32_t kCapabilityFloat16 = 9;

		inline uint32_t readWord(const char* data, size_t index) {
			uint32_t word = 0;
			std::memcpy(&word, data + index * sizeof(uint32_t), sizeof(uint32_t));
			return word;
		}
	}

	inline bool spirvDeclaresFloat16(const char* data, size_t dataLength) {
		if (!data) {
			return false;
		}
		const size_t count = dataLength / sizeof(uint32_t);
		for (size_t i = detail::kSpirvHeaderWords; i < count;) {
			const uint32_t first = detail::readWord(data, i);
			const uint32_t wordCount = first >> 16;
			if (wordCount == 0 || wordCount > count - i) {
				break;
			}
			if ((first & 0xFFFFu) == detail::kOpCapability && wordCount >= 2 && detail::readWord(data, i + 1) == detail::kCapabilityFloat16) {
				return true;
			}
			i += wordCount;
		}
		return false;
	}

	inline void requireSignedZeroFloat16ForModule(const char* data, size_t dataLength, bool deviceSupportsFloat16SignedZero) {
		if (spirvDeclaresFloat16(data, dataLength) && !deviceSupportsFloat16SignedZero) {
			throw std::runtime_error("this device does not support the 16-bit signed-zero float control that the shader module needs");
		}
	}
	class PrivateLibrary;
	class PrivateDevice;
	class SamplerState;

	struct FunctionInfo {
		FunctionType functionType;
		// note that these bindings use the same set of indices between different resource types (e.g. stage-ins, buffers, and textures)
		// like Vulkan does whereas Metal uses per-resource-type indexing.
		// the Metal-to-SPIR-V translator should output code that follows this indexing scheme.
		// in this indexing scheme, all buffers are bound first, followed by stage-ins, followed by textures, followed by samplers.
		std::vector<Iridium::BindingInfo> bindings;
		std::vector<Iridium::EmbeddedSampler> embeddedSamplers;
		std::vector<std::shared_ptr<SamplerState>> embeddedSamplerStates;
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

		INDIUM_PROPERTY(VkShaderModule, s, S,haderModule) = VK_NULL_HANDLE;
	};
};
