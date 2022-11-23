#pragma once

#include <cstddef>

#include <vector>
#include <unordered_map>
#include <string>

namespace Iridium {
	void init();
	void finit();

	enum class FunctionType {
		Vertex = 1,
		Fragment = 2,
	};

	enum class BindingType {
		Buffer,
	};

	struct BindingInfo {
		BindingType type;
	};

	struct FunctionInfo {
		FunctionType type;
		std::vector<BindingInfo> bindings;
	};

	struct OutputInfo {
		std::unordered_map<std::string, FunctionInfo> functionInfos;
	};

	/**
	 * @returns A pointer allocated with `malloc`, or `nullptr` if translation failed.
	 */
	void* translate(const void* inputData, size_t inputSize, size_t& outputSize, OutputInfo& outputInfo);
};
